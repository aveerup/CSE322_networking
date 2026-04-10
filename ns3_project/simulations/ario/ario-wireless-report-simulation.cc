#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/a-rio-queue-disc.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/energy-module.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("ArioSimulation");

class TrTCMMarker
{
public:
  TrTCMMarker (double cirBps, double pirBps)
    : m_cirBytesPerSec (cirBps / 8.0),
      m_pirBytesPerSec (pirBps / 8.0),
      m_cbsBytes (1.5 * cirBps / 8.0),
      m_pbsBytes (1.5 * pirBps / 8.0),
      m_tcBytes (m_cbsBytes),
      m_tpBytes (m_pbsBytes),
      m_lastUpdate (Simulator::Now ())
  {
  }

  uint8_t Mark (uint32_t pktBytes)
  {
    Time now = Simulator::Now ();
    double dt = (now - m_lastUpdate).GetSeconds ();
    m_lastUpdate = now;

    if (dt > 0.0)
      {
        m_tcBytes = std::min (m_cbsBytes, m_tcBytes + dt * m_cirBytesPerSec);
        m_tpBytes = std::min (m_pbsBytes, m_tpBytes + dt * m_pirBytesPerSec);
      }

    double b = static_cast<double> (pktBytes);
    if (m_tcBytes >= b && m_tpBytes >= b)
      {
        m_tcBytes -= b;
        m_tpBytes -= b;
        return 0x28; // AF11 (green)
      }

    if (m_tpBytes >= b)
      {
        m_tpBytes -= b;
        return 0x30; // AF12 (yellow)
      }

    return 0x38; // AF13 (red)
  }

private:
  double m_cirBytesPerSec;
  double m_pirBytesPerSec;
  double m_cbsBytes;
  double m_pbsBytes;
  double m_tcBytes;
  double m_tpBytes;
  Time m_lastUpdate;
};

struct FlowState
{
  Ptr<Socket> sock;
  bool isOn;
  bool useOnOff;
  bool startRequested;
  bool connected;

  Ptr<ParetoRandomVariable> onRv;
  Ptr<ParetoRandomVariable> offRv;

  std::unique_ptr<TrTCMMarker> marker;
  uint32_t pktSize;
  Time sendInterval;
};

static Ptr<QueueDisc> g_queueDisc;
static std::vector<double> g_queueSamples;
static std::array<uint64_t, 3> g_markedByPrec = {0, 0, 0};

void SampleQueue ();
void SendPacket (FlowState* state);
void StartOnPeriod (FlowState* state);
void StartOffPeriod (FlowState* state);

void
SampleQueue ()
{
  if (g_queueDisc)
    {
      g_queueSamples.push_back (g_queueDisc->GetCurrentSize ().GetValue ());
    }
  Simulator::Schedule (MilliSeconds (100), &SampleQueue);
}

void
SendPacket (FlowState* state)
{
  if (!state->isOn || !state->connected)
    {
      return;
    }

  uint8_t tos = state->marker->Mark (state->pktSize);
  state->sock->SetIpTos (tos);

  int sent = state->sock->Send (Create<Packet> (state->pktSize));
  if (sent > 0)
    {
      if (tos == 0x28)
        {
          g_markedByPrec[0]++;
        }
      else if (tos == 0x30)
        {
          g_markedByPrec[1]++;
        }
      else
        {
          g_markedByPrec[2]++;
        }
    }

  Simulator::Schedule (state->sendInterval, &SendPacket, state);
}

void
StartOnPeriod (FlowState* state)
{
  if (!state->connected)
    {
      return;
    }

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

void
RequestStartFlow (FlowState* state)
{
  state->startRequested = true;
  if (state->connected && !state->isOn)
    {
      StartOnPeriod (state);
    }
}

int
main (int argc, char* argv[])
{
  uint32_t simCase = 1;
  uint32_t assuredRate = 75;
  double simTime = 120.0;
  double targetDelay = 0.050;
  uint32_t nNodes = 100;            // vary: 20, 40, 60, 80, 100
  uint32_t nFlows = 50;             // vary: 10, 20, 30, 40, 50
  uint32_t packetsPerSecond = 100;  // vary: 100, 200, 300, 400, 500
  bool mobilityEnabled = false;     // when true, uses node speed
  double nodeSpeed = 5.0;           // m/s: 5, 10, 15, 20, 25
  double txRange = 50.0;            // reference Tx range in meters
  uint32_t areaMultiplier = 1;      // 1..5 => side = multiplier * txRange (static case)
  double initialEnergyJ = 10000.0;  // per wireless node

  CommandLine cmd;
  cmd.AddValue ("case", "Traffic case (1-6)", simCase);
  cmd.AddValue ("assuredRate", "Assured rate (% of bottleneck)", assuredRate);
  cmd.AddValue ("simTime", "Simulation time (s)", simTime);
  cmd.AddValue ("targetDelay", "A-RIO target delay (s)", targetDelay);
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

  const uint32_t nSources = nNodes;
  const uint32_t pktSize = 1000;
  const double bottleBwBps = 30e6;
  const double bottlePktPerSec = bottleBwBps / (pktSize * 8.0);

  uint32_t nFtp = 0;
  uint32_t nOnOff = 0;
  switch (simCase)
    {
    case 1: nFtp = nFlows; nOnOff = 0; break;
    case 2: nFtp = nFlows; nOnOff = 0; break;
    case 3: nFtp = 0; nOnOff = nFlows; break;
    case 4: nFtp = 0; nOnOff = nFlows; break;
    case 5: nFtp = std::max (1u, static_cast<uint32_t> (std::round (0.2 * nFlows))); nOnOff = nFlows - nFtp; break;
    case 6: nFtp = std::max (1u, static_cast<uint32_t> (std::round (0.2 * nFlows))); nOnOff = nFlows - nFtp; break;
    default: NS_FATAL_ERROR ("Invalid case");
    }

  const double totalAssuredBps = bottleBwBps * assuredRate / 100.0;
  const double perFlowCirBps = totalAssuredBps / nFlows;
  const double perFlowPirBps = 2.0 * perFlowCirBps;

  NodeContainer srcNodes, sinkNode, routerNodes;
  srcNodes.Create (nSources);
  sinkNode.Create (1);
  routerNodes.Create (2);

  InternetStackHelper internet;
  internet.Install (srcNodes);
  internet.Install (sinkNode);
  internet.Install (routerNodes);

  PointToPointHelper bottleLink;
  bottleLink.SetDeviceAttribute ("DataRate", StringValue ("500kbps"));
  bottleLink.SetChannelAttribute ("Delay", StringValue ("2ms"));
  bottleLink.SetQueue ("ns3::DropTailQueue", "MaxSize", StringValue ("1p"));

  PointToPointHelper egressLink;
  egressLink.SetDeviceAttribute ("DataRate", StringValue ("60Mbps"));
  egressLink.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer bottleDevs = bottleLink.Install (routerNodes.Get (0), routerNodes.Get (1));
  NetDeviceContainer egressDevs = egressLink.Install (routerNodes.Get (1), sinkNode.Get (0));

  // Wireless access network:
  // source nodes are STAs, router0 is AP. Wired bottleneck remains unchanged.
  NodeContainer wifiStaNodes = srcNodes;
  NodeContainer wifiApNode (routerNodes.Get (0));

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
  Ssid ssid ("ario-wifi-access");

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

  ipv4.SetBase ("10.3.0.0", "255.255.255.0");
  Ipv4InterfaceContainer staIfaces = ipv4.Assign (staDevices);
  Ipv4InterfaceContainer apIfaces = ipv4.Assign (apDevice);
  (void)staIfaces;
  (void)apIfaces;

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  TrafficControlHelper cleanTch;
  cleanTch.Uninstall (bottleDevs);

  TrafficControlHelper tch;
  tch.SetRootQueueDisc ("ns3::ARioQueueDisc",
                        "TargetDelay", DoubleValue (targetDelay),
                        "LinkBandwidth", DoubleValue (bottlePktPerSec),
                        "QueueLimit", UintegerValue (1000));

  QueueDiscContainer qdiscs = tch.Install (bottleDevs.Get (0));
  g_queueDisc = qdiscs.Get (0);

  uint16_t sinkPort = 5001;
  Address sinkAddr (InetSocketAddress (egressIfaces.GetAddress (1), sinkPort));
  PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
  ApplicationContainer sinkApps = sinkHelper.Install (sinkNode.Get (0));
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (Seconds (simTime));

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

  Time sendInterval = Seconds (1.0 / static_cast<double> (packetsPerSecond));

  for (uint32_t i = 0; i < nFlows; i++)
    {
      auto flow = std::make_unique<FlowState> ();
      flow->sock = Socket::CreateSocket (srcNodes.Get (i), UdpSocketFactory::GetTypeId ());
      flow->sock->Connect (sinkAddr);

      flow->isOn = false;
      flow->useOnOff = (i >= nFtp);
      flow->startRequested = false;
      flow->connected = true;
      flow->onRv = onRv;
      flow->offRv = offRv;
      flow->marker = std::make_unique<TrTCMMarker> (perFlowCirBps, perFlowPirBps);
      flow->pktSize = pktSize;
      flow->sendInterval = sendInterval;

      double start = startRng->GetValue ();
      Simulator::Schedule (Seconds (start), &RequestStartFlow, flow.get ());
      flows.push_back (std::move (flow));
    }

  FlowMonitorHelper flowMonHelper;
  Ptr<FlowMonitor> flowMon = flowMonHelper.InstallAll ();

  Simulator::Schedule (MilliSeconds (100), &SampleQueue);
  Simulator::Stop (Seconds (simTime + 1.0));
  Simulator::Run ();

  double avgQ = 0.0;
  uint32_t warmup = 200;
  uint32_t n = 0;
  for (uint32_t i = warmup; i < g_queueSamples.size (); i++)
    {
      avgQ += g_queueSamples[i];
      n++;
    }
  if (n > 0)
    {
      avgQ /= n;
    }

  flowMon->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowMonHelper.GetClassifier ());

  uint64_t totalTxPackets = 0;
  uint64_t totalRxPackets = 0;
  uint64_t totalRxBytes = 0;
  double totalDelaySec = 0.0;

  for (auto& kv : flowMon->GetFlowStats ())
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (kv.first);
      if (t.destinationPort == sinkPort)
        {
          totalTxPackets += kv.second.txPackets;
          totalRxPackets += kv.second.rxPackets;
          totalRxBytes += kv.second.rxBytes;
          totalDelaySec += kv.second.delaySum.GetSeconds ();
        }
    }

  double statsWindow = (simTime > 20.0) ? (simTime - 20.0) : simTime;
  statsWindow = std::max (statsWindow, 1e-6);
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

  uint64_t totalMarked = g_markedByPrec[0] + g_markedByPrec[1] + g_markedByPrec[2];

  std::cout << "=== A-RIO Simulation Results ===" << std::endl;
  std::cout << "Case:           " << simCase << std::endl;
  std::cout << "Assured Rate:   " << assuredRate << "%" << std::endl;
  std::cout << "Nodes / Flows:  " << nNodes << " / " << nFlows << std::endl;
  std::cout << "Flow Mix:       FTP=" << nFtp << " OnOff=" << nOnOff << std::endl;
  std::cout << "PPS:            " << packetsPerSecond << std::endl;
  std::cout << "Mobility:       " << (mobilityEnabled ? "ON" : "OFF")
            << "  speed=" << nodeSpeed << " m/s" << std::endl;
  std::cout << "Coverage Side:  " << areaSide << " m (" << areaMultiplier << " x TxRange)" << std::endl;
  std::cout << "Avg Queue:      " << avgQ << " packets" << std::endl;
  std::cout << "Marked Green:   " << g_markedByPrec[0] << " pkts" << std::endl;
  std::cout << "Marked Yellow:  " << g_markedByPrec[1] << " pkts" << std::endl;
  std::cout << "Marked Red:     " << g_markedByPrec[2] << " pkts" << std::endl;
  if (totalMarked > 0)
    {
      std::cout << "Marked Mix:     G=" << (100.0 * g_markedByPrec[0] / totalMarked)
                << "% Y=" << (100.0 * g_markedByPrec[1] / totalMarked)
                << "% R=" << (100.0 * g_markedByPrec[2] / totalMarked) << "%" << std::endl;
    }

  std::cout << "Network Throughput: " << (throughputBps / 1e6) << " Mbps" << std::endl;
  std::cout << "End-to-End Delay:   " << (avgDelaySec * 1000.0) << " ms" << std::endl;
  std::cout << "Packet Deliv Ratio: " << pdr << " (" << (pdr * 100.0) << "%)" << std::endl;
  std::cout << "Packet Drop Ratio:  " << dropRatio << " (" << (dropRatio * 100.0) << "%)" << std::endl;
  std::cout << "Energy Consumed:    " << totalEnergyConsumedJ << " J" << std::endl;

  ARioQueueDisc::Stats s;
  Ptr<ARioQueueDisc> ario = DynamicCast<ARioQueueDisc> (g_queueDisc);
  if (ario)
    {
      s = ario->GetStats ();
      std::cout << "Dropped Green:   " << s.dropped[0] << " pkts" << std::endl;
      std::cout << "Dropped Yellow:  " << s.dropped[1] << " pkts" << std::endl;
      std::cout << "Dropped Red:     " << s.dropped[2] << " pkts" << std::endl;
    }

  std::ofstream out("scratch/ario-wireless-report_out.csv", std::ios::app);
  out << simCase << ","
      << assuredRate << ","
      << nNodes << ","
      << nFlows << ","
      << packetsPerSecond << ","
      << (mobilityEnabled ? 1 : 0) << ","
      << nodeSpeed << ","
      << txRange << ","
      << areaMultiplier << ","
      << nFtp << ","
      << nOnOff << ","
      << avgQ << ","
    //   << g_markedByPrec[0] << ","
    //   << g_markedByPrec[1] << ","
    //   << g_markedByPrec[2] << ","
      << (throughputBps / 1e6) << ","
      << (avgDelaySec * 1000.0) << ","
      << pdr << ","
      << dropRatio << ","
      << totalEnergyConsumedJ
    //   << s.dropped[0] << ","
    //   << s.dropped[1] << ","
    //   << s.dropped[2]
      << std::endl;

  Simulator::Destroy ();
  return 0;
}
