#ifndef RIO_QUEUE_DISC_H
#define RIO_QUEUE_DISC_H

#include "ns3/queue-disc.h"
#include "ns3/nstime.h"
#include "ns3/random-variable-stream.h"

namespace ns3 {

class RioQueueDisc : public QueueDisc
{
public:
  static const uint32_t N_PREC = 3;

  static TypeId GetTypeId (void);
  RioQueueDisc ();
  virtual ~RioQueueDisc ();

  uint32_t GetDropped (uint32_t prec) const;
  double   GetAvg     (uint32_t prec) const;

private:
  virtual bool DoEnqueue (Ptr<QueueDiscItem> item) override;
  virtual Ptr<QueueDiscItem> DoDequeue (void) override;
  virtual Ptr<const QueueDiscItem> DoPeek (void) override;
  virtual bool CheckConfig (void) override;
  virtual void InitializeParams (void) override;

  uint32_t GetPrecedence (Ptr<const QueueDiscItem> item) const;
  void     UpdateAvg (uint32_t prec, bool idle);
  double   DropProbability (uint32_t prec) const;

  double m_greenMinTh;
  double m_greenMaxTh;
  double m_greenMaxP;
  double m_yellowMinTh;
  double m_yellowMaxTh;
  double m_yellowMaxP;
  double m_redMinTh;
  double m_redMaxTh;
  double m_redMaxP;
  double   m_qW;
  uint32_t m_queueLimit;
  double   m_ptc;

  double   m_minTh[N_PREC];
  double   m_maxTh[N_PREC];
  double   m_maxP[N_PREC];
  double   m_avg[N_PREC];
  uint32_t m_precCount[N_PREC];
  uint32_t m_dropped[N_PREC];
  bool     m_idle;
  Time     m_idleStart;

  Ptr<UniformRandomVariable> m_uv;
};

}
#endif
