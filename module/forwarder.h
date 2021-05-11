#pragma once

#include <queue>
#include <random>
#include <unordered_map>

#include "common/configuration.h"
#include "common/metrics.h"
#include "common/types.h"
#include "connection/broker.h"
#include "module/base/networked_module.h"
#include "proto/transaction.pb.h"
#include "storage/lookup_master_index.h"

namespace slog {

/**
 * A Forwarder determines the type of a transaction (single-home vs. multi-home)
 * then forwards it to the appropriate module.
 *
 * To determine the type of a txn, it sends LookupMasterRequests to other Forwarder
 * modules in the same region and aggregates the responses.
 *
 * INPUT:  ForwardTransaction and LookUpMasterRequest
 *
 * OUTPUT: If the txn is single-home, forward to the Sequencer in its home region.
 *         If the txn is multi-home, forward to the MultiHomeOrderer for ordering;
 *         if bypass_mh_orderer is set to true in the config, the multi-home txn is
 *         sent directly to the involved regions.
 *
 *         For LookUpMasterRequest, a LookUpMasterResponse is sent back to
 *         the requester.
 */
class Forwarder : public NetworkedModule {
 public:
  Forwarder(const std::shared_ptr<zmq::context_t>& context, const ConfigurationPtr& config,
            const std::shared_ptr<LookupMasterIndex<Key, Metadata>>& lookup_master_index,
            const MetricsRepositoryManagerPtr& metrics_manager, milliseconds poll_timeout_ms = kModuleTimeout);

 protected:
  void Initialize() final;
  void OnInternalRequestReceived(EnvelopePtr&& env) final;
  void OnInternalResponseReceived(EnvelopePtr&& env) final;

 private:
  void ScheduleNextLatencyProbe();
  void ProcessForwardTxn(EnvelopePtr&& env);
  void ProcessLookUpMasterRequest(EnvelopePtr&& env);
  void ProcessPingRequest(EnvelopePtr&& env);
  void ProcessStatsRequest(const internal::StatsRequest& stats_request);

  void SendLookupMasterRequestBatch();

  void UpdateMasterInfo(EnvelopePtr&& env);
  void UpdateLatency(EnvelopePtr&& env);

  /**
   * Pre-condition: transaction type is not UNKNOWN
   */
  void Forward(EnvelopePtr&& env);

  std::shared_ptr<LookupMasterIndex<Key, Metadata>> lookup_master_index_;
  std::unordered_map<TxnId, EnvelopePtr> pending_transactions_;
  std::vector<internal::Envelope> partitioned_lookup_request_;
  int batch_size_;
  std::vector<std::queue<int64_t>> latency_buffers_;
  std::vector<float> avg_latencies_us_;
  std::vector<int64_t> clock_offsets_us_;
  int64_t max_clock_offset_us_;

  std::mt19937 rg_;

  bool collecting_stats_;
  steady_clock::time_point batch_starting_time_;
  std::vector<int> stat_batch_sizes_;
  std::vector<float> stat_batch_durations_ms_;
};

}  // namespace slog