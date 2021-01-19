#include "module/scheduler.h"

#include <glog/logging.h>

#include <algorithm>

#include "common/json_utils.h"
#include "common/monitor.h"
#include "common/proto_utils.h"
#include "common/types.h"
#include "proto/internal.pb.h"

using std::make_shared;
using std::move;

namespace slog {

using internal::Request;
using internal::Response;

Scheduler::Scheduler(const ConfigurationPtr& config, const shared_ptr<Broker>& broker,
                     const shared_ptr<Storage<Key, Record>>& storage, std::chrono::milliseconds poll_timeout)
    : NetworkedModule("Scheduler", broker, kSchedulerChannel, poll_timeout), config_(config) {
  for (size_t i = 0; i < config->num_workers(); i++) {
    workers_.push_back(MakeRunnerFor<Worker>(config, broker, kMaxChannel + i, storage, poll_timeout));
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

std::vector<zmq::socket_t> Scheduler::InitializeCustomSockets() {
  zmq::socket_t worker_socket(*context(), ZMQ_DEALER);
  worker_socket.set(zmq::sockopt::rcvhwm, 0);
  worker_socket.set(zmq::sockopt::sndhwm, 0);
  worker_socket.bind(MakeInProcChannelAddress(kWorkerChannel));

  vector<zmq::socket_t> sockets;
  sockets.push_back(move(worker_socket));
  return sockets;
}

/***********************************************
        Internal Requests & Responses
***********************************************/

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

void Scheduler::ProcessStatsRequest(const internal::StatsRequest& stats_request) {
  using rapidjson::StringRef;

  int level = stats_request.level();

  rapidjson::Document stats;
  stats.SetObject();
  auto& alloc = stats.GetAllocator();

  // Add stats for current transactions in the system
  stats.AddMember(StringRef(NUM_ALL_TXNS), active_txns_.size(), alloc);
  if (level >= 1) {
    stats.AddMember(StringRef(ALL_TXNS),
                    ToJsonArray(
                        active_txns_, [](const auto& p) { return p.first; }, alloc),
                    alloc);
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

bool Scheduler::HandleCustomSocket(zmq::socket_t& worker_socket, size_t) {
  zmq::message_t msg;
  if (!worker_socket.recv(msg, zmq::recv_flags::dontwait)) {
    return false;
  }

  auto txn_id = *msg.data<TxnId>();
  auto& txn_holder = GetTxnHolder(txn_id);
  // Release locks held by this txn. Enqueue the txns that
  // become ready thanks to this release.
  auto unblocked_txns = lock_manager_.ReleaseLocks(txn_holder);
  for (auto unblocked_txn : unblocked_txns) {
    Dispatch(unblocked_txn);
  }

  VLOG(2) << "Released locks of txn " << txn_id;

#if defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY)
  auto txn = txn_holder.transaction();
  // If a remaster transaction, trigger any unblocked txns
  if (txn->procedure_case() == Transaction::ProcedureCase::kRemaster && txn->status() == TransactionStatus::COMMITTED) {
    auto& key = txn->write_set().begin()->first;
    auto counter = txn->internal().master_metadata().at(key).counter() + 1;
    ProcessRemasterResult(remaster_manager_.RemasterOccured(key, counter));
  }
#endif /* defined(REMASTER_PROTOCOL_SIMPLE) || \
          defined(REMASTER_PROTOCOL_PER_KEY) */

  auto it = active_txns_.find(txn_id);
  DCHECK(it != active_txns_.end());
  it->second.done = true;
  if (it->second.is_ready_for_gc()) {
    active_txns_.erase(it);
  }
  return true;
}

/***********************************************
              Transaction Processing
***********************************************/

void Scheduler::ProcessTransaction(EnvelopePtr&& env) {
  auto txn = AcceptTransaction(move(env));
  if (txn == nullptr) {
    return;
  }

  TRACE(txn->mutable_internal(), TransactionEvent::ENTER_SCHEDULER);

  auto txn_id = txn->internal().id();
  auto txn_type = txn->internal().type();
  switch (txn_type) {
    case TransactionType::SINGLE_HOME: {
      VLOG(2) << "Accepted SINGLE-HOME transaction " << txn_id;

      if (MaybeContinuePreDispatchAbort(txn_id)) {
        break;
      }

      auto& txn_holder = GetTxnHolder(txn_id);

#if defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY)
      if (txn->procedure_case() == Transaction::ProcedureCase::kRemaster) {
        if (MaybeAbortRemasterTransaction(txn)) {
          break;
        }
      }
      SendToRemasterManager(txn_holder);
#else
      SendToLockManager(txn_holder);
#endif
      break;
    }
    case TransactionType::LOCK_ONLY: {
      auto rep_id = TxnHolder::replica_id(txn);

      VLOG(2) << "Accepted LOCK-ONLY transaction " << txn_id << ", home = " << rep_id;

      if (MaybeContinuePreDispatchAbortLockOnly(txn_id)) {
        break;
      }

      auto& txn_holder = GetLockOnlyTxnHolder(txn_id, rep_id);

#if defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY)
      SendToRemasterManager(txn_holder);
#else
      SendToLockManager(txn_holder);
#endif

      break;
    }
    case TransactionType::MULTI_HOME: {
      VLOG(2) << "Accepted MULTI-HOME transaction " << txn_id;

      auto& txn_holder = GetTxnHolder(txn_id);

      if (MaybeContinuePreDispatchAbort(txn_id)) {
        break;
      }

#ifdef REMASTER_PROTOCOL_COUNTERLESS
      if (txn->procedure_case() == Transaction::ProcedureCase::kRemaster && MaybeAbortRemasterTransaction(txn)) {
        break;
      }
#endif

      SendToLockManager(txn_holder);
      break;
    }
    default:
      LOG(ERROR) << "Unknown transaction type";
      break;
  }
}

#ifdef ENABLE_REMASTER
bool Scheduler::MaybeAbortRemasterTransaction(Transaction* txn) {
  // TODO: this check can be done as soon as metadata is assigned
  auto txn_id = txn->internal().id();
  auto past_master = txn->internal().master_metadata().begin()->second.master();
  if (txn->remaster().new_master() == past_master) {
    TriggerPreDispatchAbort(txn_id);
    return true;
  }
  return false;
}
#endif

Transaction* Scheduler::AcceptTransaction(EnvelopePtr&& env) {
  auto txn = env->mutable_request()->mutable_forward_txn()->release_txn();
  auto txn_id = txn->internal().id();
  switch (txn->internal().type()) {
    case TransactionType::SINGLE_HOME: {
      auto ins = active_txns_.try_emplace(txn_id, config_, txn);
      if (!ins.second) {
        LOG(ERROR) << "Already received SINGLE-HOME txn: " << txn_id;
        return nullptr;
      }
      CHECK(!ins.first->second.txn->keys_in_partition().empty());
      break;
    }
    case TransactionType::MULTI_HOME: {
      auto ins = active_txns_.try_emplace(txn_id, config_, txn);
      auto& active_txn = ins.first->second;
      if (!ins.second) {
        if (active_txn.txn.has_value()) {
          LOG(ERROR) << "Already received MULTI-HOME txn: " << txn_id;
          return nullptr;
        }
        active_txn.txn.emplace(config_, txn);
      }
      CHECK(!active_txn.txn->keys_in_partition().empty());
      break;
    }
    case TransactionType::LOCK_ONLY: {
      auto ins = active_txns_.try_emplace(txn_id, config_, txn);
      auto& active_txn = ins.first->second;
      auto rep_id = TxnHolder::replica_id(txn);
      if (!ins.second) {
        if (active_txn.lock_only_txns[rep_id].has_value()) {
          LOG(ERROR) << "Already received LOCK-ONLY txn: (" << txn_id << ", " << rep_id << ")";
          return nullptr;
        }
        active_txn.lock_only_txns[rep_id].emplace(config_, txn);
      }
      CHECK(!active_txn.lock_only_txns[rep_id]->keys_in_partition().empty());
      break;
    }
    default:
      LOG(ERROR) << "Unknown transaction type";
      return nullptr;
  }
  return txn;
}

#if defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY)
void Scheduler::SendToRemasterManager(const TxnHolder& txn_holder) {
  auto txn = txn_holder.transaction();
  auto txn_type = txn->internal().type();
  DCHECK(txn_type == TransactionType::SINGLE_HOME || txn_type == TransactionType::LOCK_ONLY)
      << "MH aren't sent to the remaster manager";

  switch (remaster_manager_.VerifyMaster(txn_holder)) {
    case VerifyMasterResult::VALID: {
      SendToLockManager(txn_holder);
      break;
    }
    case VerifyMasterResult::ABORT: {
      TriggerPreDispatchAbort(txn->internal().id());
      break;
    }
    case VerifyMasterResult::WAITING: {
      VLOG(4) << "Txn waiting on remaster: " << txn->internal().id();
      // Do nothing
      break;
    }
    default:
      LOG(ERROR) << "Unknown VerifyMaster type";
      break;
  }
}

void Scheduler::ProcessRemasterResult(RemasterOccurredResult result) {
  for (auto unblocked_txn_holder : result.unblocked) {
    SendToLockManager(*unblocked_txn_holder);
  }
  // Check for duplicates
  // TODO: remove this set and check
  unordered_set<TxnId> aborting_txn_ids;
  for (auto unblocked_txn_holder : result.should_abort) {
    aborting_txn_ids.insert(unblocked_txn_holder->transaction()->internal().id());
  }
  CHECK_EQ(result.should_abort.size(), aborting_txn_ids.size()) << "Duplicate transactions returned for abort";
  for (auto txn_id : aborting_txn_ids) {
    TriggerPreDispatchAbort(txn_id);
  }
}
#endif /* defined(REMASTER_PROTOCOL_SIMPLE) || \
          defined(REMASTER_PROTOCOL_PER_KEY) */

void Scheduler::SendToLockManager(const TxnHolder& txn_holder) {
  auto txn_id = txn_holder.transaction()->internal().id();
  auto txn_type = txn_holder.transaction()->internal().type();
  switch (txn_type) {
    case TransactionType::SINGLE_HOME: {
      lock_manager_.AcceptTransaction(txn_holder);
      AcquireLocksAndProcessResult(txn_holder);
      break;
    }
    case TransactionType::MULTI_HOME: {
      if (lock_manager_.AcceptTransaction(txn_holder)) {
        // Note: this only records when MH arrives after lock-onlys
        TRACE(txn_holder.transaction()->mutable_internal(), TransactionEvent::ACCEPTED);

        Dispatch(txn_id);
      }
      break;
    }
    case TransactionType::LOCK_ONLY: {
      AcquireLocksAndProcessResult(txn_holder);
      break;
    }
    default:
      LOG(ERROR) << "Unknown transaction type";
      break;
  }
}

void Scheduler::AcquireLocksAndProcessResult(const TxnHolder& txn_holder) {
  auto txn_id = txn_holder.transaction()->internal().id();
  VLOG(2) << "Trying to acquires locks of txn " << txn_id;
  switch (lock_manager_.AcquireLocks(txn_holder)) {
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

/***********************************************
         Pre-Dispatch Abort Processing
***********************************************/
// Disable pre-dispatch abort when DDR is used. Removing this method is sufficient to disable the
// whole mechanism
#ifdef LOCK_MANAGER_DDR
void Scheduler::TriggerPreDispatchAbort(TxnId) {}
#else
void Scheduler::TriggerPreDispatchAbort(TxnId txn_id) {
  auto active_txn_it = active_txns_.find(txn_id);
  CHECK(active_txn_it != active_txns_.end());
  auto& active_txn = active_txn_it->second;
  CHECK(!active_txn.aborting) << "Abort was triggered twice: " << txn_id;

  VLOG(2) << "Triggering pre-dispatch abort of txn " << txn_id;

  active_txn.aborting = true;

  MaybeContinuePreDispatchAbort(txn_id);
}
#endif

bool Scheduler::MaybeContinuePreDispatchAbort(TxnId txn_id) {
  auto it = active_txns_.find(txn_id);
  if (it == active_txns_.end() || !it->second.aborting || !it->second.txn.has_value()) {
    return false;
  }

  VLOG(3) << "Main txn of abort arrived: " << txn_id;

  auto& txn_holder = it->second.txn.value();

  // Release txn from remaster manager and lock manager.
  //
  // If the abort was triggered by a remote partition,
  // then the single-home or multi-home transaction may still
  // be in one of the managers, and needs to be removed.
  //
  // This also releases any lock-only transactions.
#if defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY)
  ProcessRemasterResult(remaster_manager_.ReleaseTransaction(txn_holder));
#endif /* defined(REMASTER_PROTOCOL_SIMPLE) || \
          defined(REMASTER_PROTOCOL_PER_KEY) */

  // Release locks held by this txn. Enqueue the txns that
  // become ready thanks to this release.
  auto unblocked_txns = lock_manager_.ReleaseLocks(txn_holder);
  for (auto unblocked_txn : unblocked_txns) {
    Dispatch(unblocked_txn);
  }

  // Let a worker handle notifying other partitions and send back to the server.
  txn_holder.transaction()->set_status(TransactionStatus::ABORTED);
  Dispatch(txn_id);

  return true;
}

bool Scheduler::MaybeContinuePreDispatchAbortLockOnly(TxnId txn_id) {
  auto it = active_txns_.find(txn_id);
  if (it == active_txns_.end() || !it->second.aborting) {
    return false;
  }
  if (it->second.is_ready_for_gc()) {
    active_txns_.erase(it);
  }
  return true;
}

/***********************************************
              Transaction Dispatch
***********************************************/

void Scheduler::Dispatch(TxnId txn_id) {
  auto& txn_holder = GetTxnHolder(txn_id);

  TRACE(txn_holder.transaction()->mutable_internal(), TransactionEvent::DISPATCHED);

  zmq::message_t msg(sizeof(TxnHolder*));
  *msg.data<TxnHolder*>() = &txn_holder;
  GetCustomSocket(0).send(msg, zmq::send_flags::none);

  VLOG(2) << "Dispatched txn " << txn_id;
}

TxnHolder& Scheduler::GetTxnHolder(TxnId txn_id) {
  auto it = active_txns_.find(txn_id);
  DCHECK(it != active_txns_.end());
  DCHECK(it->second.txn.has_value());
  return it->second.txn.value();
}

TxnHolder& Scheduler::GetLockOnlyTxnHolder(TxnId txn_id, uint32_t rep_id) {
  auto active_txn_it = active_txns_.find(txn_id);
  DCHECK(active_txn_it != active_txns_.end());
  DCHECK(active_txn_it->second.lock_only_txns[rep_id].has_value());
  return active_txn_it->second.lock_only_txns[rep_id].value();
}

}  // namespace slog