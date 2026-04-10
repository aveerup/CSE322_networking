#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/a-rio-queue-disc.h"
#include "ns3/udp-socket-factory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("ArioReportSimulation");

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

  uint8_t
  Mark (uint32_t pktBytes)
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
        return 0x28;
      }

    if (m_tpBytes >= b)
      {
        m_tpBytes -= b;
        return 0x30;
      }

    return 0x38;
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
  bool connected;

  Ptr<ParetoRandomVariable> onRv;
  Ptr<ParetoRandomVariable> offRv;

  std::unique_ptr<TrTCMMarker> marker;
  uint32_t pktSize;
  Time sendInterval;
};

static Ptr<QueueDisc> g_queueDisc;
static std::vector<double> g_queueSamples;

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
  state->sock->Send (Create<Packet> (state->pktSize));

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

int
main (int argc, char* argv[])
{
  uint32_t simCase = 1;
  uint32_t assuredRate = 75;
  double simTime = 120.0;
  double targetDelay = 0.050;

  uint32_t nNodes = 100; 
  uint32_t nFlows = 50; 
  uint32_t packetsPerSecond = 100; 

  CommandLine cmd;
  cmd.AddValue ("case", "Traffic case (1-6)", simCase);
  cmd.AddValue ("assuredRate", "Assured rate (% of bottleneck)", assuredRate);
  cmd.AddValue ("simTime", "Simulation time (s)", simTime);
  cmd.AddValue ("targetDelay", "A-RIO target delay (s)", targetDelay);
  cmd.AddValue ("nNodes", "Number of source nodes", nNodes);
  cmd.AddValue ("nFlows", "Number of active flows", nFlows);
  cmd.AddValue ("pps", "Packets per second per active flow", packetsPerSecond);
  cmd.Parse (argc, argv);

  nNodes = std::max (1u, nNodes);
  nFlows = std::max (1u, std::min (nFlows, nNodes));
  packetsPerSecond = std::max (1u, packetsPerSecond);

  const uint32_t pktSize = 1000;
  const double bottleBwBps = 30e6;
  const double bottlePktPerSec = bottleBwBps / (pktSize * 8.0);

  uint32_t nFtp = 0;
  uint32_t nOnOff = 0;
  bool equalRtt = true;
  switch (simCase)
    {
    case 1: nFtp = nFlows; nOnOff = 0; equalRtt = true; break;
    case 2: nFtp = nFlows; nOnOff = 0; equalRtt = false; break;
    case 3: nFtp = 0; nOnOff = nFlows; equalRtt = true; break;
    case 4: nFtp = 0; nOnOff = nFlows; equalRtt = false; break;
    case 5: nFtp = std::max (1u, static_cast<uint32_t> (std::round (0.2 * nFlows))); nOnOff = nFlows - nFtp; equalRtt = true; break;
    case 6: nFtp = std::max (1u, static_cast<uint32_t> (std::round (0.2 * nFlows))); nOnOff = nFlows - nFtp; equalRtt = false; break;
    default: NS_FATAL_ERROR ("Invalid case");
    }

  const double totalAssuredBps = bottleBwBps * assuredRate / 100.0;
  const double perFlowCirBps = totalAssuredBps / nFlows;
  const double perFlowPirBps = 2.0 * perFlowCirBps;

  NodeContainer srcNodes, sinkNode, routerNodes;
  srcNodes.Create (nNodes);
  sinkNode.Create (1);
  routerNodes.Create (2);

  InternetStackHelper internet;
  internet.Install (srcNodes);
  internet.Install (sinkNode);
  internet.Install (routerNodes);

  PointToPointHelper accessLink;
  accessLink.SetDeviceAttribute ("DataRate", StringValue ("1Mbps"));

  PointToPointHelper bottleLink;
  bottleLink.SetDeviceAttribute ("DataRate", StringValue ("15Mbps"));
  bottleLink.SetChannelAttribute ("Delay", StringValue ("2ms"));
  bottleLink.SetQueue ("ns3::DropTailQueue", "MaxSize", StringValue ("1p"));

  PointToPointHelper egressLink;
  egressLink.SetDeviceAttribute ("DataRate", StringValue ("60Mbps"));
  egressLink.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer bottleDevs = bottleLink.Install (routerNodes.Get (0), routerNodes.Get (1));
  NetDeviceContainer egressDevs = egressLink.Install (routerNodes.Get (1), sinkNode.Get (0));

  std::vector<NetDeviceContainer> accessDevs (nNodes);
  for (uint32_t i = 0; i < nNodes; i++)
    {
      if (equalRtt)
        {
          accessLink.SetChannelAttribute ("Delay", StringValue ("50ms"));
        }
      else
        {
          accessLink.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (40 + i * 5)));
        }
      accessDevs[i] = accessLink.Install (srcNodes.Get (i), routerNodes.Get (0));
    }

  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.0.0", "255.255.255.0");
  ipv4.Assign (bottleDevs);

  ipv4.SetBase ("10.2.0.0", "255.255.255.0");
  Ipv4InterfaceContainer egressIfaces = ipv4.Assign (egressDevs);

  for (uint32_t i = 0; i < nNodes; i++)
    {
      std::ostringstream base;
      base << "10." << (3 + i / 254) << "." << (i % 254) << ".0";
      ipv4.SetBase (base.str ().c_str (), "255.255.255.0");
      ipv4.Assign (accessDevs[i]);
    }

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
      flow->connected = true;
      flow->onRv = onRv;
      flow->offRv = offRv;
      flow->marker = std::make_unique<TrTCMMarker> (perFlowCirBps, perFlowPirBps);
      flow->pktSize = pktSize;
      flow->sendInterval = sendInterval;

      double start = startRng->GetValue ();
      Simulator::Schedule (Seconds (start), &StartOnPeriod, flow.get ());
      flows.push_back (std::move (flow));
    }

  FlowMonitorHelper flowMonHelper;
  Ptr<FlowMonitor> flowMon = flowMonHelper.InstallAll ();

  Simulator::Schedule (MilliSeconds (100), &SampleQueue);
  Simulator::Stop (Seconds (simTime + 1.0));
  Simulator::Run ();

  double avgQ = 0.0;
  uint32_t warmup = 200;
  uint32_t nSamples = 0;
  for (uint32_t i = warmup; i < g_queueSamples.size (); i++)
    {
      avgQ += g_queueSamples[i];
      nSamples++;
    }
  if (nSamples > 0)
    {
      avgQ /= nSamples;
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

  std::cout << "=== A-RIO Report Simulation Results ===" << std::endl;
  std::cout << "Case:                 " << simCase << std::endl;
  std::cout << "Assured Rate:         " << assuredRate << "%" << std::endl;
  std::cout << "Nodes / Flows:        " << nNodes << " / " << nFlows << std::endl;
  std::cout << "Flow Mix:             FTP=" << nFtp << " OnOff=" << nOnOff << std::endl;
  std::cout << "Packets per second:   " << packetsPerSecond << std::endl;
  std::cout << "Avg Queue Size:       " << avgQ << " packets" << std::endl;
  std::cout << "Network Throughput:   " << throughputBps / 1e6 << " Mbps" << std::endl;
  std::cout << "End-to-End Delay:     " << avgDelaySec * 1000.0 << " ms" << std::endl;
  std::cout << "Packet Delivery Ratio:" << pdr << " (" << (pdr * 100.0) << "%)" << std::endl;
  std::cout << "Packet Drop Ratio:    " << dropRatio << " (" << (dropRatio * 100.0) << "%)" << std::endl;

  std::ofstream out ("scratch/ario-report_out.csv", std::ios::app);
  out << simCase << ","
      << assuredRate << ","
      << nNodes << ","
      << nFlows << ","
      << packetsPerSecond << ","
      << avgQ << ","
      << (throughputBps / 1e6) << ","
      << (avgDelaySec * 1000.0) << ","
      << pdr << ","
      << dropRatio
      << std::endl;

  Simulator::Destroy ();
  return 0;
}
