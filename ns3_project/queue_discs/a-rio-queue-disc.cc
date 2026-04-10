#include "a-rio-queue-disc.h"
#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/simulator.h"
#include "ns3/abort.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/socket.h"
#include <cmath>
#include <algorithm>
#include "ns3/drop-tail-queue.h"
#include "ns3/queue.h"

NS_LOG_COMPONENT_DEFINE ("ARioQueueDisc");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (ARioQueueDisc);

TypeId
ARioQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ARioQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<ARioQueueDisc> ()
    .AddAttribute ("TargetDelay",
                   "Target queuing delay in seconds",
                   DoubleValue (0.05),
                   MakeDoubleAccessor (&ARioQueueDisc::m_targetDelay),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("LinkBandwidth",
                   "Link bandwidth in packets per second",
                   DoubleValue (3750.0),   // 30 Mb/s at 1000B packets
                   MakeDoubleAccessor (&ARioQueueDisc::m_linkBw),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("QueueLimit",
                   "Maximum queue size in packets",
                   UintegerValue (1000),
                   MakeUintegerAccessor (&ARioQueueDisc::m_queueLimit),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Interval",
                   "Adaptation interval",
                   TimeValue (Seconds (0.5)),
                   MakeTimeAccessor (&ARioQueueDisc::m_interval),
                   MakeTimeChecker ())
    .AddAttribute ("Beta",
                   "Multiplicative decrease factor for maxp",
                   DoubleValue (0.9),
                   MakeDoubleAccessor (&ARioQueueDisc::m_beta),
                   MakeDoubleChecker<double> ())
  ;
  return tid;
}

ARioQueueDisc::ARioQueueDisc ()
  : m_idle (true)
{
  NS_LOG_FUNCTION (this);
  m_uv = CreateObject<UniformRandomVariable> ();
  for (uint32_t i = 0; i < N_PREC; i++)
    {
      m_avg[i]  = 0.0;

      m_precCount[i] = 0; 
      
      // Initial maxp: green=0.02, yellow=0.1, red=0.2 (Table 4 in paper)
      m_maxp[i] = (i == 0) ? 0.02 : (i == 1) ? 0.1 : 0.2;
    }
}

ARioQueueDisc::~ARioQueueDisc () {}

void
ARioQueueDisc::InitializeParams (void)
{
  NS_LOG_FUNCTION (this);

  // Replicate A-RED parameter rules (Section 2.1, paper)
  m_minth = std::max (5.0, m_targetDelay * m_linkBw / 2.0);
  m_maxth  = 3.0 * m_minth;
  m_wq     = 1.0 - std::exp (-1.0 / m_linkBw);

  // Target stabilisation interval (Section 3.2)
  m_qlow  = m_minth + 0.4 * (m_maxth - m_minth);
  m_qhigh = m_minth + 0.6 * (m_maxth - m_minth);

  NS_LOG_INFO ("A-RIO params: minth=" << m_minth
    << " maxth=" << m_maxth
    << " qlow=" << m_qlow
    << " qhigh=" << m_qhigh
    << " wq=" << m_wq);

  // Start periodic adaptation
  m_adaptTimer = Simulator::Schedule (m_interval, &ARioQueueDisc::AdaptMaxP, this);
}

bool
ARioQueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () != 0)
    {
      NS_LOG_ERROR ("ARioQueueDisc cannot have classes");
      return false;
    }
  if (GetNInternalQueues () == 0)
    {
     
     AddInternalQueue (CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>> (
        "MaxSize", QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, m_queueLimit))));
    }
  return true;
}

// ---------------------------------------------------------------------------
// Precedence extraction
// Precedence is encoded in the IP DSCP field (AF11=0/green, AF12=1/yellow, AF13=2/red)
// or can be set via a socket tag. Here we use a simple DSCP mapping.
// DSCP values per RFC 2597: AF11=10, AF12=12, AF13=14
//                            AF21=18, AF22=20, AF23=22  etc.
// For simplicity we map: DSCP&0x6 -> drop precedence (0=low, 1=med, 2=high)
// In the simulation script we set DSCP explicitly.
uint32_t
ARioQueueDisc::GetPrecedence (Ptr<const QueueDiscItem> item) const
{
  Ptr<const Ipv4QueueDiscItem> ipItem = DynamicCast<const Ipv4QueueDiscItem> (item);
  if (ipItem == nullptr) return N_PREC - 1; // default: lowest priority

  uint8_t dscp = ipItem->GetHeader ().GetDscp ();
  // RFC 2597 AFxy mapping for drop precedence y:
  // AF11 (10) -> green(0), AF12 (12) -> yellow(1), AF13 (14) -> red(2)
  // Using the AF pattern: dp = ((dscp & 0x06) >> 1), where dp=1/2/3.
  uint8_t dp = (dscp & 0x06) >> 1;
  if (dp <= 1) return 0;
  if (dp == 2) return 1;
  return 2;
}

// ---------------------------------------------------------------------------
// Coupled queue count: number of packets with precedence <= j
uint32_t
ARioQueueDisc::GetCoupledCount (uint32_t j) const
{
  uint32_t count = 0;
  Ptr<const InternalQueue> q = GetInternalQueue (0);
  // Iterate over internal queue (read-only via QueueDisc interface)
  // ns-3 doesn't provide direct iteration; we track counts via m_avg instead.
  // This is handled by UpdateAvg being called per-packet arrival.
  (void)q; (void)j;
  return count; // placeholder – we use EWMA m_avg instead (see UpdateAvg)
}

// ---------------------------------------------------------------------------
// EWMA update for coupled virtual queue j.
// q(j) = instantaneous count of packets with precedence 0..j.
// We approximate this by passing the current physical queue length
// weighted by fraction of precedence 0..j — but since we track avg[j]
// via EWMA we compute it as the paper specifies: update on each arrival.
void
ARioQueueDisc::UpdateAvg (uint32_t j)
{
  // Count packets with precedence <= j in the internal queue
  uint32_t qj = 0;
  // Walk internal queue to count coupled packets
  // ns-3's QueueDisc only exposes queue via peek/pop, not iteration.
  // Work-around: maintain a per-precedence counter array updated on enqueue/dequeue.
  // We use a simpler approach: track per-precedence packet counts separately.
  // (See m_precCount[] maintained in DoEnqueue/DoDequeue)
  for (uint32_t p = 0; p <= j; p++)
    qj += m_precCount[p];

  if (m_idle)
    {
      // RED idle path: exponential decay during idle period
      double idleTime = (Simulator::Now () - m_idleStart).GetSeconds ();
      double m = std::floor (idleTime / (-std::log (1.0 - m_wq)));
      m_avg[j] = m_avg[j] * std::pow (1.0 - m_wq, m);
    }
  else
    {
      m_avg[j] = (1.0 - m_wq) * m_avg[j] + m_wq * (double)qj;
    }
}

double
ARioQueueDisc::DropProb (uint32_t i) const
{
  // Linear interpolation between minth and maxth
  double pb = m_maxp[i] * (m_avg[i] - m_minth) / (m_maxth - m_minth);
  return pb;
}

double
ARioQueueDisc::DropProbGentle (uint32_t i) const
{
  // Gentle extension: linearly from maxp[i] to 1 between maxth and 2*maxth
  double pb = m_maxp[i] + (1.0 - m_maxp[i]) * (m_avg[i] - m_maxth) / m_maxth;
  return pb;
}

// ---------------------------------------------------------------------------
// Core enqueue logic (Figure 5 in paper)
bool
ARioQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);

  uint32_t prec_i = GetPrecedence (item);
  m_stats.enqueued[prec_i]++;

  // Hard drop if physical queue is full
  if (GetCurrentSize ().GetValue () >= m_queueLimit)
    {
      NS_LOG_DEBUG ("Queue full, dropping");
      DropBeforeEnqueue (item, "Queue full");
      m_stats.dropped[prec_i]++;
      return false;
    }

  // Update idle state
  if (m_idle)
    {
      m_idle = false;
    }

  // Update coupled averages for j = i, i+1, ..., N_PREC-1 once per arrival.
  m_precCount[prec_i]++; // include arriving packet for avg update
  for (uint32_t j = prec_i; j < N_PREC; j++)
    UpdateAvg (j);

  double p = 0.0;
  bool drop = false;

  if (m_avg[prec_i] > 2.0 * m_maxth)
    {
      // Always drop
      drop = true;
    }
  else if (m_avg[prec_i] > m_maxth)
    {
      p = DropProbGentle (prec_i);
      drop = (m_uv->GetValue () < p);
    }
  else if (m_avg[prec_i] > m_minth)
    {
      p = DropProb (prec_i);
      drop = (m_uv->GetValue () < p);
    }

  if (drop)
    {
      NS_LOG_DEBUG ("A-RIO dropping prec=" << prec_i << " avg=" << m_avg[prec_i] << " p=" << p);
      DropBeforeEnqueue (item, "A-RIO random drop");
      if (m_precCount[prec_i] > 0)
        {
          m_precCount[prec_i]--;
        }
      m_stats.dropped[prec_i]++;
      return false;
    }

  // Enqueue
  bool retval = GetInternalQueue (0)->Enqueue (item);
  if (!retval && m_precCount[prec_i] > 0)
    {
      m_precCount[prec_i]--;
    }
  return retval;
}

Ptr<QueueDiscItem>
ARioQueueDisc::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);
  Ptr<QueueDiscItem> item = GetInternalQueue (0)->Dequeue ();
  if (!item)
    {
      m_idle = true;
      m_idleStart = Simulator::Now ();
      return nullptr;
    }
  uint32_t prec_i = GetPrecedence (item);
  if (m_precCount[prec_i] > 0)
    m_precCount[prec_i]--;
  return item;
}

Ptr<const QueueDiscItem>
ARioQueueDisc::DoPeek (void)
{
  return GetInternalQueue (0)->Peek ();
}

// ---------------------------------------------------------------------------
// Periodic maxp adaptation (Figure 5 / Figure 2 in paper)
void
ARioQueueDisc::AdaptMaxP ()
{
  NS_LOG_FUNCTION (this);

  for (uint32_t j = 0; j < N_PREC; j++)
    {
      if (m_avg[j] > m_qhigh && m_maxp[j] < 0.5)
        {
          // Additive increase
          double alpha = std::min (0.01, m_maxp[j] / 4.0);
          m_maxp[j] += alpha;
          m_maxp[j] = std::min (m_maxp[j], 0.5);

          // Enforce ordering: maxp[j] <= maxp[j+1]
          if (j < N_PREC - 1)
            m_maxp[j] = std::min (m_maxp[j], m_maxp[j + 1]);
        }
      else if (m_avg[j] < m_qlow && m_maxp[j] > 0.01)
        {
          // Multiplicative decrease
          m_maxp[j] *= m_beta;
          m_maxp[j] = std::max (m_maxp[j], 0.01);

          // Enforce ordering: maxp[j] >= maxp[j-1]
          if (j > 0)
            m_maxp[j] = std::max (m_maxp[j], m_maxp[j - 1]);
        }

      NS_LOG_DEBUG ("AdaptMaxP prec=" << j
        << " avg=" << m_avg[j]
        << " maxp=" << m_maxp[j]);
    }

  // Reschedule
  m_adaptTimer = Simulator::Schedule (m_interval, &ARioQueueDisc::AdaptMaxP, this);
}

} // namespace ns3
