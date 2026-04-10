#include "rio-queue-disc.h"
#include "ns3/log.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/simulator.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/queue.h"
#include "ns3/ipv4-queue-disc-item.h"
#include <cmath>
#include <algorithm>

NS_LOG_COMPONENT_DEFINE ("RioQueueDisc");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (RioQueueDisc);

TypeId
RioQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::RioQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<RioQueueDisc> ()
    .AddAttribute ("GreenMinTh", "Green min threshold (packets)",
                   DoubleValue (350.0),
                   MakeDoubleAccessor (&RioQueueDisc::m_greenMinTh),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("GreenMaxTh", "Green max threshold (packets)",
                   DoubleValue (650.0),
                   MakeDoubleAccessor (&RioQueueDisc::m_greenMaxTh),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("GreenMaxP", "Green max drop probability",
                   DoubleValue (0.02),
                   MakeDoubleAccessor (&RioQueueDisc::m_greenMaxP),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("YellowMinTh", "Yellow min threshold (packets)",
                   DoubleValue (200.0),
                   MakeDoubleAccessor (&RioQueueDisc::m_yellowMinTh),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("YellowMaxTh", "Yellow max threshold (packets)",
                   DoubleValue (400.0),
                   MakeDoubleAccessor (&RioQueueDisc::m_yellowMaxTh),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("YellowMaxP", "Yellow max drop probability",
                   DoubleValue (0.1),
                   MakeDoubleAccessor (&RioQueueDisc::m_yellowMaxP),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("RedMinTh", "Red min threshold (packets)",
                   DoubleValue (100.0),
                   MakeDoubleAccessor (&RioQueueDisc::m_redMinTh),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("RedMaxTh", "Red max threshold (packets)",
                   DoubleValue (250.0),
                   MakeDoubleAccessor (&RioQueueDisc::m_redMaxTh),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("RedMaxP", "Red max drop probability",
                   DoubleValue (0.2),
                   MakeDoubleAccessor (&RioQueueDisc::m_redMaxP),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("QW", "EWMA weight",
                   DoubleValue (0.002),
                   MakeDoubleAccessor (&RioQueueDisc::m_qW),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("QueueLimit", "Max queue size (packets)",
                   UintegerValue (1000),
                   MakeUintegerAccessor (&RioQueueDisc::m_queueLimit),
                   MakeUintegerChecker<uint32_t> ())
  ;
  return tid;
}

RioQueueDisc::RioQueueDisc ()
  : m_greenMinTh(350), m_greenMaxTh(650), m_greenMaxP(0.02),
    m_yellowMinTh(200), m_yellowMaxTh(400), m_yellowMaxP(0.1),
    m_redMinTh(100),   m_redMaxTh(250),   m_redMaxP(0.2),
    m_qW(0.002), m_queueLimit(1000), m_ptc(3750.0),
    m_idle(true)
{
  m_uv = CreateObject<UniformRandomVariable> ();
  for (uint32_t i = 0; i < N_PREC; i++)
    {
      m_avg[i]       = 0.0;
      m_precCount[i] = 0;
      m_dropped[i]   = 0;
      m_minTh[i]     = 0.0;
      m_maxTh[i]     = 0.0;
      m_maxP[i]      = 0.0;
    }
}

RioQueueDisc::~RioQueueDisc () {}

uint32_t
RioQueueDisc::GetDropped (uint32_t prec) const
{
  return (prec < N_PREC) ? m_dropped[prec] : 0;
}

double
RioQueueDisc::GetAvg (uint32_t prec) const
{
  return (prec < N_PREC) ? m_avg[prec] : 0.0;
}

void
RioQueueDisc::InitializeParams (void)
{
  m_minTh[0] = m_greenMinTh;  m_maxTh[0] = m_greenMaxTh;  m_maxP[0] = m_greenMaxP;
  m_minTh[1] = m_yellowMinTh; m_maxTh[1] = m_yellowMaxTh; m_maxP[1] = m_yellowMaxP;
  m_minTh[2] = m_redMinTh;    m_maxTh[2] = m_redMaxTh;    m_maxP[2] = m_redMaxP;

  NS_LOG_INFO ("RIO params:"
    << " G["  << m_minTh[0] << "," << m_maxTh[0] << "] maxP=" << m_maxP[0]
    << " Y["  << m_minTh[1] << "," << m_maxTh[1] << "] maxP=" << m_maxP[1]
    << " R["  << m_minTh[2] << "," << m_maxTh[2] << "] maxP=" << m_maxP[2]
    << " qW=" << m_qW);
}

bool
RioQueueDisc::CheckConfig (void)
{
  if (GetNQueueDiscClasses () != 0)
    {
      NS_LOG_ERROR ("RioQueueDisc cannot have classes");
      return false;
    }
  if (GetNInternalQueues () == 0)
    {
      AddInternalQueue (CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>> (
        "MaxSize", QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, m_queueLimit))));
    }
  return true;
}


uint32_t
RioQueueDisc::GetPrecedence (Ptr<const QueueDiscItem> item) const
{
  Ptr<const Ipv4QueueDiscItem> ip = DynamicCast<const Ipv4QueueDiscItem> (item);
  if (!ip) return 2;

  uint8_t dscp = ip->GetHeader ().GetDscp ();
  uint8_t dp   = (dscp & 0x06) >> 1;
  if (dp <= 1) return 0;      
  if (dp == 2) return 1;      
  return 2;                   
}


void
RioQueueDisc::UpdateAvg (uint32_t prec, bool idle)
{
  uint32_t qj = 0;
  for (uint32_t p = 0; p <= prec; p++)
    qj += m_precCount[p];

  if (idle)
    {
      double idleSec = (Simulator::Now () - m_idleStart).GetSeconds ();
      double m = std::floor (m_ptc * idleSec);
      m_avg[prec] = m_avg[prec] * std::pow (1.0 - m_qW, m);
    }
  else
    {
      m_avg[prec] = (1.0 - m_qW) * m_avg[prec] + m_qW * (double)qj;
    }
}


double
RioQueueDisc::DropProbability (uint32_t prec) const
{
  double avg = m_avg[prec];
  double mn  = m_minTh[prec];
  double mx  = m_maxTh[prec];
  double mp  = m_maxP[prec];

  if (avg <= mn) return 0.0;
  if (avg >= mx) return 1.0;
  return mp * (avg - mn) / (mx - mn);
}

bool
RioQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  uint32_t prec = GetPrecedence (item);

  if (GetCurrentSize ().GetValue () >= m_queueLimit)
    {
      DropBeforeEnqueue (item, "Queue full");
      m_dropped[prec]++;
      return false;
    }

  m_precCount[prec]++;
  for (uint32_t j = prec; j < N_PREC; j++)
    UpdateAvg (j, m_idle);
  m_precCount[prec]--;

  if (m_idle) m_idle = false;

  double p    = DropProbability (prec);
  bool   drop = false;

  if (m_avg[prec] >= m_maxTh[prec])
    drop = true;
  else if (m_avg[prec] > m_minTh[prec])
    drop = (m_uv->GetValue () < p);

  if (drop)
    {
      DropBeforeEnqueue (item, "RIO probabilistic drop");
      m_dropped[prec]++;
      return false;
    }

  bool ok = GetInternalQueue (0)->Enqueue (item);
  if (ok)
    {
      m_precCount[prec]++;
      for (uint32_t j = prec; j < N_PREC; j++)
        UpdateAvg (j, false);
    }
  return ok;
}

Ptr<QueueDiscItem>
RioQueueDisc::DoDequeue (void)
{
  Ptr<QueueDiscItem> item = GetInternalQueue (0)->Dequeue ();
  if (!item)
    {
      m_idle      = true;
      m_idleStart = Simulator::Now ();
      return nullptr;
    }
  uint32_t prec = GetPrecedence (item);
  if (m_precCount[prec] > 0) m_precCount[prec]--;
  return item;
}

Ptr<const QueueDiscItem>
RioQueueDisc::DoPeek (void)
{
  return GetInternalQueue (0)->Peek ();
}

}
