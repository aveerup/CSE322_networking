#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/udp-socket-factory.h"

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
      return 0x32;
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

  CommandLine cmd;
  cmd.AddValue ("case", "Traffic case 1-6", simCase);
  cmd.AddValue ("assuredRate", "Assured rate as % of bottleneck", assuredRate);
  cmd.AddValue ("simTime", "Simulation time (s)", simTime);
  cmd.AddValue ("nNodes", "Number of source nodes", nNodes);
  cmd.AddValue ("nFlows", "Number of active flows", nFlows);
  cmd.AddValue ("pps", "Packets per second per active flow", packetsPerSecond);
  cmd.Parse (argc, argv);

  nNodes = std::max (1u, nNodes);
  nFlows = std::max (1u, std::min (nFlows, nNodes));
  packetsPerSecond = std::max (1u, packetsPerSecond);

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
  bool equalRTT = true;
  switch (simCase)
    {
    case 1: nFTP = nFlows; nOnOff = 0; equalRTT = true; break;
    case 2: nFTP = nFlows; nOnOff = 0; equalRTT = false; break;
    case 3: nFTP = 0; nOnOff = nFlows; equalRTT = true; break;
    case 4: nFTP = 0; nOnOff = nFlows; equalRTT = false; break;
    case 5: nFTP = std::max (1u, static_cast<uint32_t> (std::round (0.2 * nFlows))); nOnOff = nFlows - nFTP; equalRTT = true; break;
    case 6: nFTP = std::max (1u, static_cast<uint32_t> (std::round (0.2 * nFlows))); nOnOff = nFlows - nFTP; equalRTT = false; break;
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

  PointToPointHelper accessLink;
  accessLink.SetDeviceAttribute ("DataRate", StringValue ("1Mbps"));

  PointToPointHelper bottleLink;
  bottleLink.SetDeviceAttribute ("DataRate", StringValue ("30Mbps"));
  bottleLink.SetChannelAttribute ("Delay", StringValue ("2ms"));
  bottleLink.SetQueue ("ns3::DropTailQueue", "MaxSize", StringValue ("1p"));

  PointToPointHelper egressLink;
  egressLink.SetDeviceAttribute ("DataRate", StringValue ("60Mbps"));
  egressLink.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer bottleDevs = bottleLink.Install (routers.Get (0), routers.Get (1));
  NetDeviceContainer egressDevs = egressLink.Install (routers.Get (1), sinkNode.Get (0));

  std::vector<NetDeviceContainer> accessDevs (N_SOURCES);
  for (uint32_t i = 0; i < N_SOURCES; i++)
    {
      if (equalRTT)
        {
          accessLink.SetChannelAttribute ("Delay", StringValue ("50ms"));
        }
      else
        {
          accessLink.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (40 + i * 5))); // 40..535ms
        }
      accessDevs[i] = accessLink.Install (sources.Get (i), routers.Get (0));
    }

  Ipv4AddressHelper ipv4;

  ipv4.SetBase ("10.1.0.0", "255.255.255.0");
  ipv4.Assign (bottleDevs);

  ipv4.SetBase ("10.2.0.0", "255.255.255.0");
  Ipv4InterfaceContainer egressIfaces = ipv4.Assign (egressDevs);

  for (uint32_t i = 0; i < N_SOURCES; i++)
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

  uint64_t totalSent = g_sentByColor[0] + g_sentByColor[1] + g_sentByColor[2];

  std::cout << "\n=== RIO Report Simulation Results ===" << std::endl;
  std::cout << "Case:                 " << simCase << std::endl;
  std::cout << "Assured Rate:         " << assuredRate << "%" << std::endl;
  std::cout << "Nodes / Flows:        " << nNodes << " / " << nFlows << std::endl;
  std::cout << "Flow Mix:             FTP=" << nFTP << " OnOff=" << nOnOff << std::endl;
  std::cout << "Packets per second:   " << packetsPerSecond << std::endl;
  std::cout << "Avg Queue Size:       " << avgQ << " packets" << std::endl;
  std::cout << "Marked Green:   " << g_sentByColor[0] << " pkts" << std::endl;
  std::cout << "Marked Yellow:  " << g_sentByColor[1] << " pkts" << std::endl;
  std::cout << "Marked Red:     " << g_sentByColor[2] << " pkts" << std::endl;
  if (totalSent > 0)
    {
      std::cout << "Marked Mix:     G=" << (100.0 * g_sentByColor[0] / totalSent)
                << "% Y=" << (100.0 * g_sentByColor[1] / totalSent)
                << "% R=" << (100.0 * g_sentByColor[2] / totalSent) << "%" << std::endl;
    }
  std::cout << "Network Throughput:   " << throughputBps / 1e6 << " Mbps" << std::endl;
  std::cout << "End-to-End Delay:     " << avgDelaySec * 1000.0 << " ms" << std::endl;
  std::cout << "Packet Delivery Ratio:" << pdr << " (" << (pdr * 100.0) << "%)" << std::endl;
  std::cout << "Packet Drop Ratio:    " << dropRatio << " (" << (dropRatio * 100.0) << "%)" << std::endl;

  Ptr<RioQueueDisc> rio = DynamicCast<RioQueueDisc> (g_qdisc);
  if (rio)
    {
      std::cout << "Dropped Red:    " << rio->GetDropped (2) << " pkts" << std::endl;
      std::cout << "Dropped Yellow: " << rio->GetDropped (1) << " pkts" << std::endl;
      std::cout << "Dropped Green:  " << rio->GetDropped (0) << " pkts" << std::endl;
    }

  std::ofstream out("scratch/rio-report_out.csv", std::ios::app);
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
