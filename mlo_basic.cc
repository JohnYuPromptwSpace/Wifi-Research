#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"

#include <map>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("MLO_Simulation");

std::ofstream outFile;
std::map<uint32_t, uint32_t> staChannelMap;

void PacketTxCallback (std::string context, Ptr<const Packet> packet) {
    double time = Simulator::Now ().GetSeconds ();
    uint32_t nodeId = Simulator::GetContext ();

    uint32_t channelId = staChannelMap.count(nodeId) ? staChannelMap[nodeId] : 0;

    outFile << "TX," << nodeId << "," << time << "," << packet->GetSize () << "," << channelId << "\n";
    NS_LOG_INFO ("[TX] Node: " << nodeId 
                 << ", Channel: " << channelId 
                 << ", Packet Size: " << packet->GetSize () 
                 << " bytes, Time: " << time << "s");
}

void PacketRxCallback (std::string context, Ptr<const Packet> packet) {
    double time = Simulator::Now ().GetSeconds ();
    uint32_t nodeId = Simulator::GetContext ();

    outFile << "RX," << nodeId << "," << time << "," << packet->GetSize () << "\n";
    NS_LOG_INFO ("[RX] Node: " << nodeId 
                 << ", Packet Size: " << packet->GetSize () 
                 << " bytes, Time: " << time << "s");
}

int main (int argc, char *argv[]) {
    uint32_t nWifi = 5;
    uint32_t nChannels = 3;
    double simulationTime = 6.0;

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

        mac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssids[i]), "QosSupported", BooleanValue (true));
        apDevices[i] = wifi.Install (phys[i], mac, wifiApNode);

        mac.SetType ("ns3::StaWifiMac", "Ssid", SsidValue (ssids[i]), "QosSupported", BooleanValue (true));
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
        Ptr<UniformRandomVariable> interval = CreateObject<UniformRandomVariable>();
        interval->SetAttribute("Min", DoubleValue(0.025));
        interval->SetAttribute("Max", DoubleValue(0.025));
        echoClient.SetAttribute ("Interval", TimeValue (Seconds (interval->GetValue())));
        Ptr<UniformRandomVariable> packetSizeRand = CreateObject<UniformRandomVariable>();
        packetSizeRand->SetAttribute("Min", DoubleValue(1024));
        packetSizeRand->SetAttribute("Max", DoubleValue(1024));
        echoClient.SetAttribute ("PacketSize", UintegerValue (packetSizeRand->GetValue()));

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

    Simulator::Destroy ();
    outFile.close ();
    return 0;
}