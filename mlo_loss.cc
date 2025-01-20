#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"

#include <map>
#include <fstream>
#include <random>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("MLO_Simulation");

std::ofstream outFile;

double packetLossProbability = 0.1;

uint32_t totalTxPackets = 0;
uint32_t totalRxPackets = 0;
uint32_t totalLostPackets = 0;
double totalDelay = 0.0;

std::map<uint32_t, double> packetSendTimeMap;

std::map<uint32_t, uint32_t> staChannelMap;

std::default_random_engine generator;
std::uniform_real_distribution<double> lossDistribution(0.0, 1.0);
std::uniform_real_distribution<double> intervalDistribution(0.01, 0.04);
std::uniform_int_distribution<uint32_t> packetSizeDistribution(500, 1500);

void PacketTxCallback (std::string context, Ptr<const Packet> packet) {
    double currentTime = Simulator::Now ().GetSeconds ();
    
    std::string nodeIdStr = context.substr(context.find("NodeList/") + 9);
    nodeIdStr = nodeIdStr.substr(0, nodeIdStr.find("/"));
    uint32_t nodeId = std::stoi(nodeIdStr);
    
    uint32_t channelId = staChannelMap.count(nodeId) ? staChannelMap[nodeId] : 0;
    
    totalTxPackets++;
    
    uint32_t packetUid = packet->GetUid();
    
    double randValue = lossDistribution(generator);
    
    if (randValue < packetLossProbability) {
        totalLostPackets++;
        outFile << "[TX] Packet loss simulated, Node: " << nodeId
                << ", Channel: " << channelId
                << ", Packet UID: " << packetUid << "\n";
        NS_LOG_INFO ("[TX] Packet loss simulated, Node: " << nodeId
                     << ", Channel: " << channelId
                     << ", Packet UID: " << packetUid);
        return;
    }
    
    outFile << "[TX] Node: " << nodeId
            << ", Channel: " << channelId
            << ", Packet Size: " << packet->GetSize()
            << " bytes, Time: " << currentTime << "s\n";
    NS_LOG_INFO ("[TX] Node: " << nodeId
                 << ", Channel: " << channelId
                 << ", Packet Size: " << packet->GetSize()
                 << " bytes, Time: " << currentTime << "s");
    
    packetSendTimeMap[packetUid] = currentTime;
}

void PacketRxCallback (std::string context, Ptr<const Packet> packet) {
    double currentTime = Simulator::Now ().GetSeconds ();
    
    std::string nodeIdStr = context.substr(context.find("NodeList/") + 9);
    nodeIdStr = nodeIdStr.substr(0, nodeIdStr.find("/"));
    uint32_t nodeId = std::stoi(nodeIdStr);
    
    uint32_t channelId = staChannelMap.count(nodeId) ? staChannelMap[nodeId] : 0;
    
    uint32_t packetUid = packet->GetUid();
    
    if (packetSendTimeMap.find(packetUid) == packetSendTimeMap.end()) {
        outFile << "[RX] Packet lost, Node: " << nodeId
                << ", Channel: " << channelId
                << ", UID: " << packetUid << "\n";
        NS_LOG_INFO ("[RX] Packet lost, Node: " << nodeId
                     << ", Channel: " << channelId
                     << ", UID: " << packetUid);
        return;
    }
    
    double sendTime = packetSendTimeMap[packetUid];
    double delay = currentTime - sendTime;
    totalDelay += delay;
    totalRxPackets++;
    
    outFile << "[RX] Node: " << nodeId
            << ", Channel: " << channelId
            << ", Packet Size: " << packet->GetSize()
            << " bytes, Time: " << currentTime << "s, Delay: " << delay << "s\n";
    NS_LOG_INFO ("[RX] Node: " << nodeId
                 << ", Channel: " << channelId
                 << ", Packet Size: " << packet->GetSize()
                 << " bytes, Time: " << currentTime << "s, Delay: " << delay << "s");
    
    packetSendTimeMap.erase(packetUid);
}

int main (int argc, char *argv[]) {
    uint32_t nWifi = 5;
    uint32_t nChannels = 3;
    double simulationTime = 6.0;
    
    outFile.open("mlo_results.txt");
    if (!outFile.is_open()) {
        std::cerr << "Failed to open mlo_results.txt for writing.\n";
        return -1;
    }
    
    LogComponentEnable ("MLO_Simulation", LOG_LEVEL_INFO);
    
    NodeContainer wifiApNode;
    wifiApNode.Create (1);
    NodeContainer wifiStaNodes;
    wifiStaNodes.Create (nWifi);
    
    std::vector<YansWifiChannelHelper> channels(nChannels);
    std::vector<YansWifiPhyHelper> phys(nChannels);
    std::vector<NetDeviceContainer> apDevices(nChannels);
    std::vector<NetDeviceContainer> staDevices(nChannels);
    std::vector<Ssid> ssids(nChannels);
    
    WifiMacHelper mac;
    WifiHelper wifi;    
    wifi.SetStandard (WIFI_STANDARD_80211ax);
    
    for (uint32_t i = 0; i < nChannels; ++i) {
        channels[i] = YansWifiChannelHelper::Default ();
        phys[i].SetChannel (channels[i].Create ());
        ssids[i] = Ssid ("ns-3-link" + std::to_string(i + 1));
        
        mac.SetType ("ns3::ApWifiMac",
                     "Ssid", SsidValue (ssids[i]),
                     "QosSupported", BooleanValue (true));
        apDevices[i] = wifi.Install (phys[i], mac, wifiApNode);
        
        mac.SetType ("ns3::StaWifiMac",
                     "Ssid", SsidValue (ssids[i]),
                     "QosSupported", BooleanValue (true));
        staDevices[i] = wifi.Install (phys[i], mac, wifiStaNodes);
    }
    
    InternetStackHelper stack;
    stack.Install (wifiApNode);
    stack.Install (wifiStaNodes);
    
    Ipv4AddressHelper address;
    std::vector<Ipv4InterfaceContainer> apInterfaces(nChannels);
    std::vector<Ipv4InterfaceContainer> staInterfaces(nChannels);
    
    for (uint32_t i = 0; i < nChannels; ++i) {
        std::string baseAddress = "192.168." + std::to_string(i + 1) + ".0";
        address.SetBase (baseAddress.c_str(), "255.255.255.0");
        apInterfaces[i] = address.Assign (apDevices[i]);
        staInterfaces[i] = address.Assign (staDevices[i]);
    }
    
    UdpEchoServerHelper echoServer (9);
    ApplicationContainer serverApps = echoServer.Install (wifiApNode.Get (0));
    serverApps.Start (Seconds (1.0));
    serverApps.Stop (Seconds (simulationTime));
    
    for (uint32_t i = 0; i < nWifi; ++i) {
        uint32_t channelId = i % nChannels;
        staChannelMap[i] = channelId;
        
        UdpEchoClientHelper echoClient (apInterfaces[channelId].GetAddress (0), 9);
        echoClient.SetAttribute ("MaxPackets", UintegerValue (100));
        
        double randomInterval = intervalDistribution(generator);
        echoClient.SetAttribute ("Interval", TimeValue (Seconds (randomInterval)));
        
        uint32_t packetSize = packetSizeDistribution(generator);
        echoClient.SetAttribute ("PacketSize", UintegerValue (packetSize));
        
        ApplicationContainer clientApps = echoClient.Install (wifiStaNodes.Get (i));
        clientApps.Start (Seconds (1.0));
        clientApps.Stop (Seconds (simulationTime));
    }
    
    MobilityHelper mobility;
    mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                   "MinX", DoubleValue (0.0),
                                   "MinY", DoubleValue (0.0),
                                   "DeltaX", DoubleValue (5.0),
                                   "DeltaY", DoubleValue (5.0),
                                   "GridWidth", UintegerValue (3),
                                   "LayoutType", StringValue ("RowFirst"));
    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    mobility.Install (wifiApNode);
    mobility.Install (wifiStaNodes);
    
    Config::Connect ("/NodeList/*/ApplicationList/*/$ns3::UdpEchoClient/Tx",
                     MakeCallback (&PacketTxCallback));
    Config::Connect ("/NodeList/*/ApplicationList/*/$ns3::UdpEchoServer/Rx",
                     MakeCallback (&PacketRxCallback));
    
    Simulator::Stop (Seconds (simulationTime));
    Simulator::Run ();
    
    double packetLossRatio = (double)totalLostPackets / totalTxPackets * 100.0;
    double averageDelay = totalRxPackets > 0 ? totalDelay / totalRxPackets : 0.0;
    double throughput = (totalRxPackets * 1024 * 8) / simulationTime / 1e6;
    
    outFile << "[MLO Mode] Total Tx Packets: " << totalTxPackets << "\n";
    outFile << "[MLO Mode] Total Rx Packets: " << totalRxPackets << "\n";
    outFile << "[MLO Mode] Packet Loss Ratio: " << packetLossRatio << "%\n";
    outFile << "[MLO Mode] Average Delay: " << averageDelay << "s\n";
    outFile << "[MLO Mode] Throughput: " << throughput << " Mbps\n";
    
    NS_LOG_INFO ("[MLO Mode] Total Tx Packets: " << totalTxPackets);
    NS_LOG_INFO ("[MLO Mode] Total Rx Packets: " << totalRxPackets);
    NS_LOG_INFO ("[MLO Mode] Packet Loss Ratio: " << packetLossRatio << "%");
    NS_LOG_INFO ("[MLO Mode] Average Delay: " << averageDelay << "s");
    NS_LOG_INFO ("[MLO Mode] Throughput: " << throughput << " Mbps");
    
    Simulator::Destroy ();
    outFile.close ();
    return 0;
}