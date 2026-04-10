#include "ns3/core-module.h" 
#include "ns3/network-module.h" 
#include "ns3/internet-module.h" 
#include "ns3/point-to-point-module.h" 
#include "ns3/applications-module.h" 
#include "ns3/traffic-control-module.h" 
#include "ns3/flow-monitor-module.h" 
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"

#include <algorithm>
#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("RedSimulation");

static Ptr<QueueDisc> g_queueDisc;
static std::vector<double> g_queueSamples;

void
SampleQueue ()
{
  if (g_queueDisc)
    {
      g_queueSamples.push_back (g_queueDisc->GetCurrentSize ().GetValue ());
    }
  Simulator::Schedule (MilliSeconds (100), &SampleQueue);
}

int
main (int argc, char* argv[])
{
  uint32_t nSources = 100;
  double simTime = 120.0;
  uint32_t simCase = 1;
  uint32_t assuredRate = 75;

  CommandLine cmd;
  cmd.AddValue ("nSources", "Number of sources (paper baseline uses 100)", nSources);
  cmd.AddValue ("simTime", "Simulation duration (s)", simTime);
  cmd.AddValue ("case", "Traffic case 1-6", simCase);
  cmd.AddValue ("assuredRate", "Assured rate profile (% of bottleneck): 25/50/75/100/125", assuredRate);
  cmd.Parse (argc, argv);

  const uint32_t pktSize = 1000;
  const double bottleBW = 30e6;
  const uint32_t queueLimit = 1000;

  const double redMinTh = 94.0;
  const double redMaxTh = 281.0;
  const double redMaxP = 0.1;
  const double redQW = 0.03;

  uint32_t nFTP = 0;
  uint32_t nOnOff = 0;
  bool equalRTT = true;
  switch (simCase)
    {
    case 1: nFTP = nSources; nOnOff = 0; equalRTT = true; break;
    case 2: nFTP = nSources; nOnOff = 0; equalRTT = false; break;
    case 3: nFTP = 0; nOnOff = nSources; equalRTT = true; break;
    case 4: nFTP = 0; nOnOff = nSources; equalRTT = false; break;
    case 5: nFTP = std::min (20u, nSources); nOnOff = nSources - nFTP; equalRTT = true; break;
    case 6: nFTP = std::min (20u, nSources); nOnOff = nSources - nFTP; equalRTT = false; break;
    default: NS_FATAL_ERROR ("Invalid case (must be 1..6)");
    }

  const double totalAssuredBps = bottleBW * assuredRate / 100.0;
  const double perFlowCIR = totalAssuredBps / std::max (1u, nSources);
  const double perFlowPIR = 2.0 * perFlowCIR;

  NodeContainer sources, routers, sink;
  sources.Create (nSources);
  routers.Create (2);
  sink.Create (1);

  InternetStackHelper internet;
  internet.Install (sources);
  internet.Install (routers);
  internet.Install (sink);

  PointToPointHelper bottleLink;
  bottleLink.SetDeviceAttribute ("DataRate", StringValue ("1Mbps"));
  bottleLink.SetChannelAttribute ("Delay", StringValue ("2ms"));
  bottleLink.SetQueue ("ns3::DropTailQueue", "MaxSize", StringValue ("1p"));

  PointToPointHelper egressLink;
  egressLink.SetDeviceAttribute ("DataRate", StringValue ("60Mbps"));
  egressLink.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer bottleDevs = bottleLink.Install (routers.Get (0), routers.Get (1));
  NetDeviceContainer egressDevs = egressLink.Install (routers.Get (1), sink.Get (0));

    NodeContainer wifiStaNodes = sources;
    NodeContainer wifiApNode = NodeContainer (routers.Get (0));

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
    YansWifiPhyHelper phy;
    phy.SetChannel (channel.Create ());

    WifiHelper wifi;
    wifi.SetStandard (WIFI_STANDARD_80211g);
    wifi.SetRemoteStationManager ("ns3::AarfWifiManager");

    WifiMacHelper mac;
    Ssid ssid = Ssid ("wifi-access");

    mac.SetType ("ns3::StaWifiMac",
                "Ssid", SsidValue (ssid),
                "ActiveProbing", BooleanValue (false));

    NetDeviceContainer staDevices = wifi.Install (phy, mac, wifiStaNodes);

    // AP device
    mac.SetType ("ns3::ApWifiMac",
                "Ssid", SsidValue (ssid));

    NetDeviceContainer apDevice = wifi.Install (phy, mac, wifiApNode);

    MobilityHelper mobility;

    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    mobility.Install (wifiStaNodes);

    MobilityHelper apMobility;
    apMobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    apMobility.Install (wifiApNode);

  Ipv4AddressHelper ipv4;

  ipv4.SetBase ("10.1.0.0", "255.255.255.0");
  ipv4.Assign (bottleDevs);

  ipv4.SetBase ("10.2.0.0", "255.255.255.0");
  Ipv4InterfaceContainer egressIfaces = ipv4.Assign (egressDevs);

  ipv4.SetBase ("10.3.0.0", "255.255.255.0");
  Ipv4InterfaceContainer staIfaces = ipv4.Assign (staDevices);
  Ipv4InterfaceContainer apIface = ipv4.Assign (apDevice);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  TrafficControlHelper cleanTch;
  cleanTch.Uninstall (bottleDevs);

  TrafficControlHelper tch;
  tch.SetRootQueueDisc ("ns3::RedQueueDisc",
                        "MinTh", DoubleValue (redMinTh),
                        "MaxTh", DoubleValue (redMaxTh),
                        "MaxSize", QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, queueLimit)),
                        "QW", DoubleValue (redQW),
                        "LInterm", DoubleValue (1.0 / redMaxP),
                        "Gentle", BooleanValue (true),
                        "LinkBandwidth", DataRateValue (DataRate ("30Mbps")),
                        "LinkDelay", TimeValue (MilliSeconds (2)));

  QueueDiscContainer qdiscs = tch.Install (bottleDevs.Get (0));
  g_queueDisc = qdiscs.Get (0);

  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (15 * pktSize));
  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (15 * pktSize));
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType",
                      TypeIdValue (TypeId::LookupByName ("ns3::TcpNewReno")));

  uint16_t port = 5001;
  Address sinkAddr (InetSocketAddress (egressIfaces.GetAddress (1), port));
  PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApp = sinkHelper.Install (sink.Get (0));
  sinkApp.Start (Seconds (0.0));
  sinkApp.Stop (Seconds (simTime));

  Ptr<UniformRandomVariable> startRng = CreateObject<UniformRandomVariable> ();
  startRng->SetAttribute ("Min", DoubleValue (2.0));
  startRng->SetAttribute ("Max", DoubleValue (12.0));

  for (uint32_t i = 0; i < nFTP; i++)
    {
      BulkSendHelper ftp ("ns3::TcpSocketFactory", sinkAddr);
      ftp.SetAttribute ("MaxBytes", UintegerValue (0));
      ftp.SetAttribute ("SendSize", UintegerValue (pktSize));
      ApplicationContainer app = ftp.Install (sources.Get (i));
      app.Start (Seconds (startRng->GetValue ()));
      app.Stop (Seconds (simTime));
    }

  for (uint32_t i = nFTP; i < nFTP + nOnOff; i++)
    {
      OnOffHelper onoff ("ns3::TcpSocketFactory", sinkAddr);
      onoff.SetAttribute ("DataRate", DataRateValue (DataRate ("1Mbps")));
      onoff.SetAttribute ("PacketSize", UintegerValue (pktSize));
      onoff.SetAttribute ("MaxBytes", UintegerValue (0));
      onoff.SetAttribute ("OnTime",
                          StringValue ("ns3::ParetoRandomVariable[Bound=20.0|Shape=1.5|Scale=0.116]"));
      onoff.SetAttribute ("OffTime",
                          StringValue ("ns3::ParetoRandomVariable[Bound=20.0|Shape=1.5|Scale=0.217]"));
      ApplicationContainer app = onoff.Install (sources.Get (i));
      app.Start (Seconds (startRng->GetValue ()));
      app.Stop (Seconds (simTime));
    }

  FlowMonitorHelper flowMonHelper;
  Ptr<FlowMonitor> flowMon = flowMonHelper.InstallAll ();

  Simulator::Schedule (MilliSeconds (100), &SampleQueue);

  Simulator::Stop (Seconds (simTime + 1.0));
  Simulator::Run ();

  double avgQ = 0.0;
  uint32_t skip = 200, count = 0;
  for (uint32_t i = skip; i < g_queueSamples.size (); i++)
    {
      avgQ += g_queueSamples[i];
      count++;
    }
  if (count > 0)
    {
      avgQ /= count;
    }

  flowMon->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowMonHelper.GetClassifier ());

  double totalBps = 0;
  uint32_t nFlows = 0;
  const double statsWindow = std::max (simTime - 20.0, 1e-6);
  for (auto& kv : flowMon->GetFlowStats ())
    {
      if (classifier->FindFlow (kv.first).destinationPort == port)
        {
          totalBps += kv.second.rxBytes * 8.0 / statsWindow;
          nFlows++;
        }
    }

  std::cout << "\n=== RED Simulation Results ===" << std::endl;
  std::cout << "Case:           " << simCase << std::endl;
  std::cout << "Assured Rate:   " << assuredRate << "% (profile knob for RIO comparability)" << std::endl;
  std::cout << "Sources:        " << nSources << " (FTP=" << nFTP << ", OnOff=" << nOnOff << ")" << std::endl;
  std::cout << "Avg Queue Size: " << avgQ << " packets" << std::endl;
  std::cout << "RED min/max:    " << redMinTh << " / " << redMaxTh << "  maxP=" << redMaxP << "  qW=" << redQW << std::endl;
  std::cout << "Per-flow CIR/PIR (for profile set): " << perFlowCIR / 1e3 << " / " << perFlowPIR / 1e3 << " kbps" << std::endl;
  if (nFlows > 0)
    {
      std::cout << "Link Utilization: " << (100.0 * totalBps / bottleBW) << "%" << std::endl;
    }
  // link utilization > 100% seems impossible, but it doesn't mean the carrier is transporting
  // data more than it's maximum capacity. If we see the formula for the link utilization -
  // link utilization = total traffic sent (or offered)/ link capacity * 100%
  // link_utilization more than 100% means the offered load was more than the carrier capacity
  // this excess traffic is dropped in the process while sending

  std::ofstream out("scratch/red-wireless_out.csv", std::ios::app);
  out << simCase << "," 
      << assuredRate << "," 
      << nSources << ","
      << nFTP << ","
      << nOnOff << "," 
      << avgQ << "," 
      << redMinTh << "," 
      << redMaxTh << ","
      << redMaxP << ","
      << redQW
      << std::endl; 
  
  Simulator::Destroy ();
  return 0;
}
