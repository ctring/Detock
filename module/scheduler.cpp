#include "module/scheduler.h"

#include <algorithm>

#include "common/json_utils.h"
#include "common/monitor.h"
#include "common/proto_utils.h"
#include "common/types.h"
#include "proto/internal.pb.h"

using std::make_shared;
using std::move;
using std::shared_ptr;

namespace slog {

using internal::Request;
using internal::Response;

Scheduler::Scheduler(const ConfigurationPtr& config, const shared_ptr<Broker>& broker,
                     const shared_ptr<Storage<Key, Record>>& storage, std::chrono::milliseconds poll_timeout)
    : NetworkedModule("Scheduler", broker, {kSchedulerChannel, false /* recv_raw */}, poll_timeout), config_(config) {
  for (size_t i = 0; i < config->num_workers(); i++) {
    workers_.push_back(MakeRunnerFor<Worker>(config, broker, Worker::MakeChannel(i), storage, poll_timeout));
  }

#if defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY)
  remaster_manager_.SetStorage(storage);
#endif

#ifdef LOCK_MANAGER_DDR
  if (config_->ddr_interval() > 0ms) {
    lock_manager_.StartDeadlockResolver(*broker->context(), kSchedulerChannel, config_->ddr_interval());
  }
#endif
}

void Scheduler::Initialize() {
  for (auto& worker : workers_) {
    worker->StartInNewThread();
  }
}

// Custom sockets are used to communicate with the workers
std::vector<zmq::socket_t> Scheduler::InitializeCustomSockets() {
  zmq::socket_t worker_socket(*context(), ZMQ_DEALER);
  worker_socket.set(zmq::sockopt::rcvhwm, 0);
  worker_socket.set(zmq::sockopt::sndhwm, 0);
  worker_socket.bind(MakeInProcChannelAddress(kWorkerChannel));

  vector<zmq::socket_t> sockets;
  sockets.push_back(move(worker_socket));
  return sockets;
}

void Scheduler::HandleInternalRequest(EnvelopePtr&& env) {
  switch (env->request().type_case()) {
    case Request::kForwardTxn:
      ProcessTransaction(move(env));
      break;
#ifdef LOCK_MANAGER_DDR
    case Request::kSignal: {
      auto ready_txns = lock_manager_.GetReadyTxns();
      for (auto ready_txns : ready_txns) {
        Dispatch(ready_txns);
      }
      break;
    }
#endif
    case Request::kStats:
      ProcessStatsRequest(env->request().stats());
      break;
    default:
      LOG(ERROR) << "Unexpected request type received: \"" << CASE_NAME(env->request().type_case(), Request) << "\"";
      break;
  }
}

/**
 * {
 *    num_all_txns: <number of active txns>,
 *    all_txns (lvl == 0): [<txn id>, ...],
 *    all_txns (lvl >= 1): [
 *      {
 *        id: <txn id>,
 *        done: <is done>,
 *        aborting: <is aborting>,
 *        num_lo: <num lock only txns>,
 *        expected_num_lo: <expected num lock only txns>,
 *      },
 *      ...
 *    ],
 *    ...<stats from lock manager>...
 * }
 */
void Scheduler::ProcessStatsRequest(const internal::StatsRequest& stats_request) {
  using rapidjson::StringRef;

  int level = stats_request.level();

  rapidjson::Document stats;
  stats.SetObject();
  auto& alloc = stats.GetAllocator();

  // Add stats for current transactions in the system
  stats.AddMember(StringRef(NUM_ALL_TXNS), active_txns_.size(), alloc);
  if (level == 0) {
    stats.AddMember(StringRef(ALL_TXNS),
                    ToJsonArray(
                        active_txns_, [](const auto& p) { return p.first; }, alloc),
                    alloc);
  }

  if (level >= 1) {
    rapidjson::Value txns(rapidjson::kArrayType);
    for (const auto& kv : active_txns_) {
      rapidjson::Value txn_obj(rapidjson::kObjectType);
      txn_obj.AddMember(StringRef(TXN_ID), kv.first, alloc)
          .AddMember(StringRef(TXN_DONE), kv.second.is_done(), alloc)
          .AddMember(StringRef(TXN_ABORTING), kv.second.is_aborting(), alloc)
          .AddMember(StringRef(TXN_NUM_LO), kv.second.num_lock_only_txns(), alloc)
          .AddMember(StringRef(TXN_EXPECTED_NUM_LO), kv.second.expected_num_lock_only_txns(), alloc);
      txns.PushBack(txn_obj, alloc);
    }
    stats.AddMember(StringRef(ALL_TXNS), txns, alloc);
  }

  // Add stats from the lock manager
  lock_manager_.GetStats(stats, level);

  // Write JSON object to a buffer and send back to the server
  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
  stats.Accept(writer);

  auto env = NewEnvelope();
  env->mutable_response()->mutable_stats()->set_id(stats_request.id());
  env->mutable_response()->mutable_stats()->set_stats_json(buf.GetString());
  Send(move(env), kServerChannel);
}

// Handle responses from the workers
bool Scheduler::HandleCustomSocket(zmq::socket_t& worker_socket, size_t) {
  zmq::message_t msg;
  if (!worker_socket.recv(msg, zmq::recv_flags::dontwait)) {
    return false;
  }

  auto txn_id = *msg.data<TxnId>();
  // Release locks held by this txn then dispatch the txns that become ready thanks to this release.
  auto unblocked_txns = lock_manager_.ReleaseLocks(txn_id);
  for (auto unblocked_txn : unblocked_txns) {
    Dispatch(unblocked_txn);
  }

  VLOG(2) << "Released locks of txn " << txn_id;

  auto it = active_txns_.find(txn_id);
  DCHECK(it != active_txns_.end());
  auto& txn_holder = it->second;

#if defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY)
  auto remaster_result = txn_holder.remaster_result();
  // If a remaster transaction, trigger any unblocked txns
  if (remaster_result.has_value()) {
    ProcessRemasterResult(remaster_manager_.RemasterOccured(remaster_result->first, remaster_result->second));
  }
#endif /* defined(REMASTER_PROTOCOL_SIMPLE) || \
          defined(REMASTER_PROTOCOL_PER_KEY) */

  txn_holder.SetDone();

  if (txn_holder.is_ready_for_gc()) {
    active_txns_.erase(it);
  }

  return true;
}

void Scheduler::ProcessTransaction(EnvelopePtr&& env) {
  auto txn = env->mutable_request()->mutable_forward_txn()->release_txn();
  auto txn_id = txn->internal().id();
  auto ins = active_txns_.try_emplace(txn_id, config_, txn);

  if (ins.second) {
    TRACE(txn->mutable_internal(), TransactionEvent::ENTER_SCHEDULER);

    VLOG(2) << "Accepted " << ENUM_NAME(txn->internal().type(), TransactionType) << " transaction (" << txn_id << ", "
            << txn->internal().home() << ")";
  } else {
    if (!ins.first->second.AddLockOnlyTxn(txn)) {
      LOG(ERROR) << "Already received txn: (" << txn_id << ", " << txn->internal().home() << ")";
      return;
    }

    VLOG(2) << "Added " << ENUM_NAME(txn->internal().type(), TransactionType) << " transaction (" << txn_id << ", "
            << txn->internal().home() << ")";
  }

  if (ins.first->second.is_aborting()) {
    if (ins.first->second.is_ready_for_gc()) {
      active_txns_.erase(ins.first);
    }
    return;
  }

#if defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY)
  SendToRemasterManager(*txn);
#else
  SendToLockManager(*txn);
#endif
}

#if defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY)
void Scheduler::SendToRemasterManager(const Transaction& txn) {
  switch (remaster_manager_.VerifyMaster(txn)) {
    case VerifyMasterResult::VALID: {
      SendToLockManager(txn);
      break;
    }
    case VerifyMasterResult::ABORT: {
      TriggerPreDispatchAbort(txn.internal().id());
      break;
    }
    case VerifyMasterResult::WAITING: {
      VLOG(4) << "Txn waiting on remaster: " << txn.internal().id();
      // Do nothing
      break;
    }
    default:
      LOG(ERROR) << "Unknown VerifyMaster type";
      break;
  }
}

void Scheduler::ProcessRemasterResult(RemasterOccurredResult result) {
  for (auto unblocked_lo : result.unblocked) {
    SendToLockManager(*unblocked_lo);
  }
  // Check for duplicates
  // TODO: remove this set and check
  unordered_set<TxnId> aborting_txn_ids;
  for (auto unblocked_lo : result.should_abort) {
    aborting_txn_ids.insert(unblocked_lo->internal().id());
  }
  CHECK_EQ(result.should_abort.size(), aborting_txn_ids.size()) << "Duplicate transactions returned for abort";
  for (auto txn_id : aborting_txn_ids) {
    TriggerPreDispatchAbort(txn_id);
  }
}
#endif /* defined(REMASTER_PROTOCOL_SIMPLE) || \
          defined(REMASTER_PROTOCOL_PER_KEY) */

void Scheduler::SendToLockManager(const Transaction& txn) {
  auto txn_id = txn.internal().id();

  VLOG(2) << "Trying to acquires locks of txn " << txn_id;

  switch (lock_manager_.AcquireLocks(txn)) {
    case AcquireLocksResult::ACQUIRED:
      Dispatch(txn_id);
      break;
    case AcquireLocksResult::ABORT:
      TriggerPreDispatchAbort(txn_id);
      break;
    case AcquireLocksResult::WAITING:
      VLOG(2) << "Txn " << txn_id << " cannot be dispatched yet";
      break;
    default:
      LOG(ERROR) << "Unknown lock result type";
      break;
  }
}

void Scheduler::Dispatch(TxnId txn_id) {
  auto it = active_txns_.find(txn_id);
  auto& txn_holder = it->second;

  TRACE(txn_holder.txn().mutable_internal(), TransactionEvent::DISPATCHED);

  zmq::message_t msg(sizeof(TxnHolder*));
  *msg.data<TxnHolder*>() = &txn_holder;
  GetCustomSocket(0).send(msg, zmq::send_flags::none);

  VLOG(2) << "Dispatched txn " << txn_id;
}

// Disable pre-dispatch abort when DDR is used. Removing this method is sufficient to disable the
// whole mechanism
#ifdef LOCK_MANAGER_DDR
void Scheduler::TriggerPreDispatchAbort(TxnId) {}
#else
void Scheduler::TriggerPreDispatchAbort(TxnId txn_id) {
  auto active_txn_it = active_txns_.find(txn_id);
  CHECK(active_txn_it != active_txns_.end());
  auto& txn_holder = active_txn_it->second;

  CHECK(!txn_holder.is_aborting()) << "Abort was triggered twice: " << txn_id;

  VLOG(2) << "Triggering pre-dispatch abort of txn " << txn_id;

  txn_holder.SetAborting();

  auto& txn = txn_holder.txn();

#if defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY)
  // Release txn from remaster manager and lock manager.
  //
  // If the abort was triggered by a remote partition,
  // then the single-home or multi-home transaction may still
  // be in one of the managers, and needs to be removed.
  //
  // This also releases any lock-only transactions.
  ProcessRemasterResult(remaster_manager_.ReleaseTransaction(txn));
#endif /* defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY) */

  // Release locks held by this txn. Enqueue the txns that
  // become ready thanks to this release.
  auto unblocked_txns = lock_manager_.ReleaseLocks(txn_id);
  for (auto unblocked_txn : unblocked_txns) {
    Dispatch(unblocked_txn);
  }

  // Let a worker handle notifying other partitions and send back to the server.
  txn.set_status(TransactionStatus::ABORTED);
  Dispatch(txn_id);
}
#endif /* LOCK_MANAGER_DDR */

}  // namespace slog