#ifndef A_RIO_QUEUE_DISC_H
#define A_RIO_QUEUE_DISC_H

#include "ns3/queue-disc.h"
#include "ns3/nstime.h"
#include "ns3/random-variable-stream.h"
#include <vector>

namespace ns3 {

/**
 * A-RIO Queue Discipline
 * Orozco & Ros, IRISA PI-1526, 2003
 *
 * Design:
 *   - N_PREC=3 precedence levels: 0=green(highest), 1=yellow, 2=red(lowest)
 *   - Overlapped thresholds: same minth/maxth for all precedences
 *   - Coupled virtual queues: avg[j] counts packets of precedences 0..j
 *   - Per-precedence adaptive maxp[i], constrained maxp[i] <= maxp[i+1]
 *   - AIMD adaptation every 0.5s
 *   - Gentle RED variant throughout
 */
class ARioQueueDisc : public QueueDisc
{
public:
  static const uint32_t N_PREC = 3;

  struct Stats {
    uint32_t dropped[N_PREC];
    uint32_t enqueued[N_PREC];
    Stats() { for(uint32_t i=0;i<N_PREC;i++){dropped[i]=0;enqueued[i]=0;} }
  };

  static TypeId GetTypeId (void);
  ARioQueueDisc ();
  virtual ~ARioQueueDisc ();

  Stats GetStats () const { return m_stats; }
  double GetAvgQueue (uint32_t prec) const { return (prec<N_PREC)?m_avg[prec]:0.0; }
  double GetMaxP (uint32_t prec) const { return (prec<N_PREC)?m_maxp[prec]:0.0; }

private:
  virtual bool DoEnqueue (Ptr<QueueDiscItem> item) override;
  virtual Ptr<QueueDiscItem> DoDequeue (void) override;
  virtual Ptr<const QueueDiscItem> DoPeek (void) override;
  virtual bool CheckConfig (void) override;
  virtual void InitializeParams (void) override;

  uint32_t GetPrecedence (Ptr<const QueueDiscItem> item) const;
  uint32_t GetCoupledCount (uint32_t j) const;
  uint32_t m_precCount[N_PREC];   // live packet count per precedence
  void UpdateAvg (uint32_t j);
  double DropProb (uint32_t i) const;
  double DropProbGentle (uint32_t i) const;
  void AdaptMaxP ();

  // Attributes
  double   m_targetDelay;
  double   m_linkBw;      // packets/s
  uint32_t m_queueLimit;
  Time     m_interval;
  double   m_beta;

  // Computed
  double m_minth, m_maxth, m_qlow, m_qhigh, m_wq;

  // State per precedence
  double   m_avg[N_PREC];
  double   m_maxp[N_PREC];

  bool   m_idle;
  Time   m_idleStart;
  EventId m_adaptTimer;
  Ptr<UniformRandomVariable> m_uv;
  Stats  m_stats;
};

} // namespace ns3
#endif
