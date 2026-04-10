#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/energy-module.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("RioSimulation");

static Ptr<QueueDisc> g_qdisc;
static std::vector<double> g_qSamples;

struct FlowState
{
  Ptr<Socket> sock;
  bool isOn;
  bool useOnOff;

  Ptr<ParetoRandomVariable> onRv;
  Ptr<ParetoRandomVariable> offRv;

  double cirBps;
  double pirBps;
  double cbsBytes;
  double pbsBytes;
  double tcBytes;
  double tpBytes;
  Time lastTokenUpdate;

  uint32_t pktSize;
  Time sendInterval;
};

static std::array<uint64_t, 3> g_sentByColor = {0, 0, 0};

void
SampleQueue ()
{
  if (g_qdisc)
    {
      g_qSamples.push_back (g_qdisc->GetCurrentSize ().GetValue ());
    }
  Simulator::Schedule (MilliSeconds (100), &SampleQueue);
}

void
UpdateTrTcmTokens (FlowState* state)
{
  Time now = Simulator::Now ();
  double dt = (now - state->lastTokenUpdate).GetSeconds ();
  if (dt <= 0.0)
    {
      return;
    }

  state->tcBytes = std::min (state->cbsBytes, state->tcBytes + dt * (state->cirBps / 8.0));
  state->tpBytes = std::min (state->pbsBytes, state->tpBytes + dt * (state->pirBps / 8.0));
  state->lastTokenUpdate = now;
}

uint32_t
MarkWithTrTcm (FlowState* state)
{
  const double b = static_cast<double> (state->pktSize);
  UpdateTrTcmTokens (state);

  if (state->tcBytes >= b && state->tpBytes >= b)
    {
      state->tcBytes -= b;
      state->tpBytes -= b;
      return 0;
    }
  if (state->tpBytes >= b)
    {
      state->tpBytes -= b;
      return 1;
    }
  return 2; 
}

uint8_t
PrecedenceToTos (uint32_t prec)
{
  if (prec == 0)
    {
      return 0x28;
    }
  if (prec == 1)
    {
      return 0x30;
    }
  return 0x38;
}

void SendPacket (FlowState* state);

void
SendPacket (FlowState* state)
{
  if (!state->isOn)
    {
      return;
    }

  uint32_t prec = MarkWithTrTcm (state);
  uint8_t tos = PrecedenceToTos (prec);

  state->sock->SetIpTos (tos);
  int sent = state->sock->Send (Create<Packet> (state->pktSize));
  if (sent > 0)
    {
      g_sentByColor[prec]++;
    }

  Simulator::Schedule (state->sendInterval, &SendPacket, state);
}

void StartOnPeriod (FlowState* state);
void StartOffPeriod (FlowState* state);

void
StartOnPeriod (FlowState* state)
{
  state->isOn = true;
  SendPacket (state);

  if (state->useOnOff)
    {
      double onTime = state->onRv->GetValue ();
      Simulator::Schedule (Seconds (onTime), &StartOffPeriod, state);
    }
}

void
StartOffPeriod (FlowState* state)
{
  state->isOn = false;

  if (state->useOnOff)
    {
      double offTime = state->offRv->GetValue ();
      Simulator::Schedule (Seconds (offTime), &StartOnPeriod, state);
    }
}

int
main (int argc, char* argv[])
{
  uint32_t simCase = 1;
  uint32_t assuredRate = 75;
  double simTime = 120.0;
  uint32_t nNodes = 100;
  uint32_t nFlows = 50;
  uint32_t packetsPerSecond = 100;
  bool mobilityEnabled = false;
  double nodeSpeed = 5.0;
  double txRange = 50.0;
  uint32_t areaMultiplier = 1;
  double initialEnergyJ = 10000.0;

  CommandLine cmd;
  cmd.AddValue ("case", "Traffic case 1-6", simCase);
  cmd.AddValue ("assuredRate", "Assured rate as % of bottleneck", assuredRate);
  cmd.AddValue ("simTime", "Simulation time (s)", simTime);
  cmd.AddValue ("nNodes", "Number of source nodes", nNodes);
  cmd.AddValue ("nFlows", "Number of active flows", nFlows);
  cmd.AddValue ("pps", "Packets per second per active flow", packetsPerSecond);
  cmd.AddValue ("mobility", "Enable mobility (true/false)", mobilityEnabled);
  cmd.AddValue ("nodeSpeed", "Node speed in m/s when mobility=true", nodeSpeed);
  cmd.AddValue ("txRange", "Reference transmission range in meters", txRange);
  cmd.AddValue ("areaMultiplier", "Coverage side multiplier (1..5) for static case", areaMultiplier);
  cmd.AddValue ("initialEnergyJ", "Initial energy per wireless node in Joules", initialEnergyJ);
  cmd.Parse (argc, argv);

  nNodes = std::max (1u, nNodes);
  nFlows = std::max (1u, std::min (nFlows, nNodes));
  packetsPerSecond = std::max (1u, packetsPerSecond);
  nodeSpeed = std::max (0.1, nodeSpeed);
  txRange = std::max (1.0, txRange);
  areaMultiplier = std::max (1u, std::min (areaMultiplier, 5u));
  initialEnergyJ = std::max (1.0, initialEnergyJ);

  const uint32_t N_SOURCES = nNodes;
  const uint32_t pktSize = 1000;
  const double bottleBW = 30e6;
  const uint32_t queueLimit = 1000;

  const double redMinTh = 100.0, redMaxTh = 250.0, redMaxP = 0.2;
  const double yellowMinTh = 200.0, yellowMaxTh = 400.0, yellowMaxP = 0.1;
  const double greenMinTh = 350.0, greenMaxTh = 600.0, greenMaxP = 0.02;
  const double qW = 0.002;

  uint32_t nFTP = 0;
  uint32_t nOnOff = 0;
  switch (simCase)
    {
    case 1: nFTP = nFlows; nOnOff = 0; break;
    case 2: nFTP = nFlows; nOnOff = 0; break;
    case 3: nFTP = 0; nOnOff = nFlows; break;
    case 4: nFTP = 0; nOnOff = nFlows; break;
    case 5: nFTP = std::max (1u, static_cast<uint32_t> (std::round (0.2 * nFlows))); nOnOff = nFlows - nFTP; break;
    case 6: nFTP = std::max (1u, static_cast<uint32_t> (std::round (0.2 * nFlows))); nOnOff = nFlows - nFTP; break;
    default: NS_FATAL_ERROR ("Invalid case");
    }

  double totalAssuredBps = bottleBW * assuredRate / 100.0;
  double perFlowCIR = totalAssuredBps / nFlows;
  double perFlowPIR = 2.0 * perFlowCIR;

  double perFlowCIRBytes = perFlowCIR / 8.0;
  double perFlowPIRBytes = perFlowPIR / 8.0;
  double perFlowCBS = 1.5 * perFlowCIRBytes;
  double perFlowPBS = 1.5 * perFlowPIRBytes;

  NodeContainer sources, routers, sinkNode;
  sources.Create (N_SOURCES);
  routers.Create (2);
  sinkNode.Create (1);

  InternetStackHelper internet;
  internet.Install (sources);
  internet.Install (routers);
  internet.Install (sinkNode);

  PointToPointHelper bottleLink;    
  bottleLink.SetDeviceAttribute ("DataRate", StringValue ("500kbps"));
  bottleLink.SetChannelAttribute ("Delay", StringValue ("2ms"));
  bottleLink.SetQueue ("ns3::DropTailQueue", "MaxSize", StringValue ("1p"));

  PointToPointHelper egressLink;
  egressLink.SetDeviceAttribute ("DataRate", StringValue ("60Mbps"));
  egressLink.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer bottleDevs = bottleLink.Install (routers.Get (0), routers.Get (1));
  NetDeviceContainer egressDevs = egressLink.Install (routers.Get (1), sinkNode.Get (0));

  NodeContainer wifiStaNodes = sources;
  NodeContainer wifiApNode (routers.Get (0));

  YansWifiChannelHelper channel;
  channel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  channel.AddPropagationLoss ("ns3::RangePropagationLossModel",
                              "MaxRange", DoubleValue (txRange));
  YansWifiPhyHelper phy;
  phy.SetChannel (channel.Create ());

  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211g);
  wifi.SetRemoteStationManager ("ns3::AarfWifiManager");

  WifiMacHelper mac;
  Ssid ssid ("rio-wifi-access");

  mac.SetType ("ns3::StaWifiMac",
               "Ssid", SsidValue (ssid),
               "ActiveProbing", BooleanValue (false));
  NetDeviceContainer staDevices = wifi.Install (phy, mac, wifiStaNodes);

  mac.SetType ("ns3::ApWifiMac",
               "Ssid", SsidValue (ssid));
  NetDeviceContainer apDevice = wifi.Install (phy, mac, wifiApNode);

  const double areaSide = static_cast<double> (areaMultiplier) * txRange;

  MobilityHelper staMobility;
  if (!mobilityEnabled)
    {
      staMobility.SetPositionAllocator ("ns3::RandomRectanglePositionAllocator",
                                        "X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string (areaSide) + "]"),
                                        "Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string (areaSide) + "]"));
      staMobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    }
  else
    {
      std::string speedAttr = "ns3::ConstantRandomVariable[Constant=" + std::to_string (nodeSpeed) + "]";
      staMobility.SetPositionAllocator ("ns3::RandomRectanglePositionAllocator",
                                        "X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string (areaSide) + "]"),
                                        "Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string (areaSide) + "]"));
      staMobility.SetMobilityModel ("ns3::RandomWalk2dMobilityModel",
                                    "Mode", StringValue ("Time"),
                                    "Time", TimeValue (Seconds (1.0)),
                                    "Speed", StringValue (speedAttr),
                                    "Bounds", RectangleValue (Rectangle (0.0, areaSide, 0.0, areaSide)));
    }
  staMobility.Install (wifiStaNodes);

  MobilityHelper apMobility;
  apMobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  apMobility.Install (wifiApNode);
  wifiApNode.Get (0)->GetObject<MobilityModel> ()->SetPosition (Vector (0.0, 0.0, 0.0));

  Ipv4AddressHelper ipv4;

  ipv4.SetBase ("10.1.0.0", "255.255.255.0");
  ipv4.Assign (bottleDevs);

  ipv4.SetBase ("10.2.0.0", "255.255.255.0");
  Ipv4InterfaceContainer egressIfaces = ipv4.Assign (egressDevs);

  // Single subnet for Wi-Fi STAs + AP.
  ipv4.SetBase ("10.3.0.0", "255.255.255.0");
  Ipv4InterfaceContainer staIfaces = ipv4.Assign (staDevices);
  Ipv4InterfaceContainer apIfaces = ipv4.Assign (apDevice);
  (void)staIfaces;
  (void)apIfaces;

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  TrafficControlHelper cleanTch;
  cleanTch.Uninstall (bottleDevs);

  TrafficControlHelper tch;
  tch.SetRootQueueDisc ("ns3::RioQueueDisc",
                        "GreenMinTh", DoubleValue (greenMinTh),
                        "GreenMaxTh", DoubleValue (greenMaxTh),
                        "GreenMaxP", DoubleValue (greenMaxP),
                        "YellowMinTh", DoubleValue (yellowMinTh),
                        "YellowMaxTh", DoubleValue (yellowMaxTh),
                        "YellowMaxP", DoubleValue (yellowMaxP),
                        "RedMinTh", DoubleValue (redMinTh),
                        "RedMaxTh", DoubleValue (redMaxTh),
                        "RedMaxP", DoubleValue (redMaxP),
                        "QW", DoubleValue (qW),
                        "QueueLimit", UintegerValue (queueLimit));

  QueueDiscContainer qdiscs = tch.Install (bottleDevs.Get (0));
  g_qdisc = qdiscs.Get (0);

  uint16_t port = 5001;
  Address sinkAddr (InetSocketAddress (egressIfaces.GetAddress (1), port));
  PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApp = sinkHelper.Install (sinkNode.Get (0));
  sinkApp.Start (Seconds (0.0));
  sinkApp.Stop (Seconds (simTime));

  Ptr<UniformRandomVariable> startRng = CreateObject<UniformRandomVariable> ();
  startRng->SetAttribute ("Min", DoubleValue (2.0));
  startRng->SetAttribute ("Max", DoubleValue (12.0));

  Ptr<ParetoRandomVariable> onRv = CreateObject<ParetoRandomVariable> ();
  onRv->SetAttribute ("Shape", DoubleValue (1.5));
  onRv->SetAttribute ("Scale", DoubleValue (0.116));
  onRv->SetAttribute ("Bound", DoubleValue (20.0));

  Ptr<ParetoRandomVariable> offRv = CreateObject<ParetoRandomVariable> ();
  offRv->SetAttribute ("Shape", DoubleValue (1.5));
  offRv->SetAttribute ("Scale", DoubleValue (0.217));
  offRv->SetAttribute ("Bound", DoubleValue (20.0));

  NodeContainer energyNodes;
  energyNodes.Add (wifiStaNodes);
  energyNodes.Add (wifiApNode);

  NetDeviceContainer energyDevices;
  for (uint32_t i = 0; i < staDevices.GetN (); ++i)
    {
      energyDevices.Add (staDevices.Get (i));
    }
  for (uint32_t i = 0; i < apDevice.GetN (); ++i)
    {
      energyDevices.Add (apDevice.Get (i));
    }

  BasicEnergySourceHelper basicSourceHelper;
  basicSourceHelper.Set ("BasicEnergySourceInitialEnergyJ", DoubleValue (initialEnergyJ));
  ns3::energy::EnergySourceContainer energySources = basicSourceHelper.Install (energyNodes);

  WifiRadioEnergyModelHelper radioEnergyHelper;
  radioEnergyHelper.Install (energyDevices, energySources);

  std::vector<std::unique_ptr<FlowState>> flows;
  flows.reserve (nFlows);

  Time peakInterval = Seconds (1.0 / static_cast<double> (packetsPerSecond));

  for (uint32_t i = 0; i < nFlows; i++)
    {
      auto state = std::make_unique<FlowState> ();
      state->sock = Socket::CreateSocket (sources.Get (i), UdpSocketFactory::GetTypeId ());
      state->sock->Connect (sinkAddr);
      state->isOn = false;
      state->useOnOff = (i >= nFTP);
      state->onRv = onRv;
      state->offRv = offRv;
      state->cirBps = perFlowCIR;
      state->pirBps = perFlowPIR;
      state->cbsBytes = perFlowCBS;
      state->pbsBytes = perFlowPBS;
      state->tcBytes = perFlowCBS;
      state->tpBytes = perFlowPBS;
      state->lastTokenUpdate = Seconds (0.0);
      state->pktSize = pktSize;
      state->sendInterval = peakInterval;

      double startTime = startRng->GetValue ();
      Simulator::Schedule (Seconds (startTime), &StartOnPeriod, state.get ());
      flows.push_back (std::move (state));
    }

  FlowMonitorHelper flowMonHelper;
  Ptr<FlowMonitor> flowMon = flowMonHelper.InstallAll ();

  Simulator::Schedule (MilliSeconds (100), &SampleQueue);

  Simulator::Stop (Seconds (simTime + 1.0));
  Simulator::Run ();

  double avgQ = 0.0;
  uint32_t skip = 200, count = 0;
  for (uint32_t i = skip; i < g_qSamples.size (); i++)
    {
      avgQ += g_qSamples[i];
      count++;
    }
  if (count > 0)
    {
      avgQ /= count;
    }

  flowMon->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowMonHelper.GetClassifier ());

  uint64_t totalTxPackets = 0;
  uint64_t totalRxPackets = 0;
  uint64_t totalRxBytes = 0;
  double totalDelaySec = 0.0;
  double statsWindow = (simTime > 20.0) ? (simTime - 20.0) : simTime;
  statsWindow = std::max (statsWindow, 1e-6);
  for (auto& kv : flowMon->GetFlowStats ())
    {
      if (classifier->FindFlow (kv.first).destinationPort == port)
        {
          totalTxPackets += kv.second.txPackets;
          totalRxPackets += kv.second.rxPackets;
          totalRxBytes += kv.second.rxBytes;
          totalDelaySec += kv.second.delaySum.GetSeconds ();
        }
    }

  double throughputBps = static_cast<double> (totalRxBytes) * 8.0 / statsWindow;
  double avgDelaySec = (totalRxPackets > 0) ? (totalDelaySec / static_cast<double> (totalRxPackets)) : 0.0;
  double pdr = (totalTxPackets > 0) ? (static_cast<double> (totalRxPackets) / static_cast<double> (totalTxPackets)) : 0.0;
  double dropRatio = (totalTxPackets > 0) ? (static_cast<double> (totalTxPackets - totalRxPackets) / static_cast<double> (totalTxPackets)) : 0.0;

  double totalInitialEnergyJ = initialEnergyJ * energyNodes.GetN ();
  double totalRemainingEnergyJ = 0.0;
  for (uint32_t i = 0; i < energySources.GetN (); i++)
    {
      Ptr<ns3::energy::BasicEnergySource> src =
        DynamicCast<ns3::energy::BasicEnergySource> (energySources.Get (i));
      if (src)
        {
          totalRemainingEnergyJ += src->GetRemainingEnergy ();
        }
    }
  double totalEnergyConsumedJ = std::max (0.0, totalInitialEnergyJ - totalRemainingEnergyJ);

  uint64_t totalSent = g_sentByColor[0] + g_sentByColor[1] + g_sentByColor[2];

  std::cout << "\n=== RIO Wireless Report Simulation Results ===" << std::endl;
  std::cout << "Case:           " << simCase << std::endl;
  std::cout << "Assured Rate:   " << assuredRate << "%" << std::endl;
  std::cout << "Nodes / Flows:  " << nNodes << " / " << nFlows << std::endl;
  std::cout << "Flow Mix:       FTP=" << nFTP << " OnOff=" << nOnOff << std::endl;
  std::cout << "PPS:            " << packetsPerSecond << std::endl;
  std::cout << "Mobility:       " << (mobilityEnabled ? "ON" : "OFF")
            << "  speed=" << nodeSpeed << " m/s" << std::endl;
  std::cout << "Coverage Side:  " << areaSide << " m (" << areaMultiplier << " x TxRange)" << std::endl;
  std::cout << "Avg Queue Size: " << avgQ << " packets" << std::endl;
  std::cout << "Marked Green:   " << g_sentByColor[0] << " pkts" << std::endl;
  std::cout << "Marked Yellow:  " << g_sentByColor[1] << " pkts" << std::endl;
  std::cout << "Marked Red:     " << g_sentByColor[2] << " pkts" << std::endl;
  if (totalSent > 0)
    {
      std::cout << "Marked Mix:     G=" << (100.0 * g_sentByColor[0] / totalSent)
                << "% Y=" << (100.0 * g_sentByColor[1] / totalSent)
                << "% R=" << (100.0 * g_sentByColor[2] / totalSent) << "%" << std::endl;
    }
  std::cout << "Network Throughput: " << (throughputBps / 1e6) << " Mbps" << std::endl;
  std::cout << "End-to-End Delay:   " << (avgDelaySec * 1000.0) << " ms" << std::endl;
  std::cout << "Packet Deliv Ratio: " << pdr << " (" << (pdr * 100.0) << "%)" << std::endl;
  std::cout << "Packet Drop Ratio:  " << dropRatio << " (" << (dropRatio * 100.0) << "%)" << std::endl;
  std::cout << "Energy Consumed:    " << totalEnergyConsumedJ << " J" << std::endl;

  Ptr<RioQueueDisc> rio = DynamicCast<RioQueueDisc> (g_qdisc);
  uint64_t dRed = 0, dYellow = 0, dGreen = 0;
  if (rio)
    {
      dRed = rio->GetDropped (2);
      dYellow = rio->GetDropped (1);
      dGreen = rio->GetDropped (0);
      std::cout << "Dropped Red:    " << dRed << " pkts" << std::endl;
      std::cout << "Dropped Yellow: " << dYellow << " pkts" << std::endl;
      std::cout << "Dropped Green:  " << dGreen << " pkts" << std::endl;
    }

  std::ofstream out("scratch/rio-wireless-report_out.csv", std::ios::app);
  out << simCase << ","
      << assuredRate << ","
      << nNodes << ","
      << nFlows << ","
      << packetsPerSecond << ","
      << (mobilityEnabled ? 1 : 0) << ","
      << nodeSpeed << ","
      << txRange << ","
      << areaMultiplier << ","
      << nFTP << ","
      << nOnOff << ","
      << avgQ << ","
    //   << g_sentByColor[0] << ","
    //   << g_sentByColor[1] << ","
    //   << g_sentByColor[2] << ","
      << (throughputBps / 1e6) << ","
      << (avgDelaySec * 1000.0) << ","
      << pdr << ","
      << dropRatio << ","
      << totalEnergyConsumedJ << ","
    //   << dGreen << ","
    //   << dYellow << ","
    //   << dRed
      << std::endl;

  Simulator::Destroy ();
  return 0;
}
