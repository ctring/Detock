#include "module/scheduler_components/worker.h"

#if defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY)
#include "module/scheduler_components/remaster_manager.h"
#endif /* defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY) */

#include <glog/logging.h>

#include <thread>

#include "common/monitor.h"
#include "common/proto_utils.h"
#include "module/scheduler.h"

using std::make_pair;

namespace slog {

namespace {
uint32_t MakeTag(const RunId& run_id) { return run_id.first * 10 + run_id.second; }
}

using internal::Envelope;
using internal::Request;
using internal::Response;

Worker::Worker(const ConfigurationPtr& config, const std::shared_ptr<Broker>& broker, Channel channel,
               const shared_ptr<Storage<Key, Record>>& storage, std::chrono::milliseconds poll_timeout)
    : NetworkedModule("Worker-" + std::to_string(channel), broker, channel, poll_timeout),
      config_(config),
      storage_(storage),
      // TODO: change this dynamically based on selected experiment
      commands_(new KeyValueCommands()) {}

void Worker::Initialize() {
  zmq::socket_t sched_socket(*context(), ZMQ_DEALER);
  sched_socket.set(zmq::sockopt::rcvhwm, 0);
  sched_socket.set(zmq::sockopt::sndhwm, 0);
  sched_socket.connect(MakeInProcChannelAddress(kWorkerChannel));

  AddCustomSocket(std::move(sched_socket));
}

void Worker::OnInternalRequestReceived(EnvelopePtr&& env) {
  if (env->request().type_case() != Request::kRemoteReadResult) {
    LOG(FATAL) << "Invalid request for worker";
  }
  auto& read_result = env->request().remote_read_result();
  auto run_id = make_pair(read_result.txn_id(), read_result.deadlocked());
  auto state_it = txn_states_.find(run_id);
  if (state_it == txn_states_.end()) {
    VLOG(1) << "Transaction " << run_id << " does not exist for remote read result";
    return;
  }

  VLOG(2) << "Got remote read result for txn " << run_id;

  auto& state = state_it->second;
  auto& txn = state.txn_holder->txn();

  if (txn.status() != TransactionStatus::ABORTED) {
    if (read_result.will_abort()) {
      // TODO: optimize by returning an aborting transaction to the scheduler immediately.
      // later remote reads will need to be garbage collected.
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason(read_result.abort_reason());
    } else {
      // Apply remote reads.
      for (const auto& kv : read_result.reads()) {
        txn.mutable_keys()->insert(kv);
      }
    }
  }

  state.remote_reads_waiting_on--;

  // Move the transaction to a new phase if all remote reads arrive
  if (state.remote_reads_waiting_on == 0) {
    if (state.phase == TransactionState::Phase::WAIT_REMOTE_READ) {
      state.phase = TransactionState::Phase::EXECUTE;
      VLOG(3) << "Execute txn " << run_id << " after receving all remote read results";
    } else {
      LOG(FATAL) << "Invalid phase";
    }
  }

  AdvanceTransaction(run_id);
}

bool Worker::OnCustomSocket() {
  auto& sched_socket = GetCustomSocket(0);

  zmq::message_t msg;
  if (!sched_socket.recv(msg, zmq::recv_flags::dontwait)) {
    return false;
  }

  auto txn_holder = *msg.data<TxnHolder*>();
  auto run_id = txn_holder->run_id();
  auto& txn = txn_holder->txn();

  TRACE(txn.mutable_internal(), TransactionEvent::ENTER_WORKER);

  // Create a state for the new transaction
  // TODO: Clean up txns that later got into a deadlock
  auto [iter, ok] = txn_states_.try_emplace(run_id, txn_holder);

  DCHECK(ok) << "Transaction " << run_id << " has already been dispatched to this worker";

  iter->second.phase = TransactionState::Phase::READ_LOCAL_STORAGE;

  // Establish a redirection at broker for this txn so that we can receive remote reads
  auto redirect_env = NewEnvelope();
  redirect_env->mutable_request()->mutable_broker_redirect()->set_tag(MakeTag(run_id));
  redirect_env->mutable_request()->mutable_broker_redirect()->set_channel(channel());
  Send(move(redirect_env), kBrokerChannel);

  VLOG(3) << "Initialized state for txn " << run_id;

  AdvanceTransaction(run_id);

  return true;
}

void Worker::AdvanceTransaction(const RunId& run_id) {
  auto& state = TxnState(run_id);
  switch (state.phase) {
    case TransactionState::Phase::READ_LOCAL_STORAGE:
      ReadLocalStorage(run_id);
      [[fallthrough]];
    case TransactionState::Phase::WAIT_REMOTE_READ:
      if (state.phase == TransactionState::Phase::WAIT_REMOTE_READ) {
        // The only way to get out of this phase is through remote messages
        break;
      }
      [[fallthrough]];
    case TransactionState::Phase::EXECUTE:
      if (state.phase == TransactionState::Phase::EXECUTE) {
        Execute(run_id);
      }
      [[fallthrough]];
    case TransactionState::Phase::COMMIT:
      if (state.phase == TransactionState::Phase::COMMIT) {
        Commit(run_id);
      }
      [[fallthrough]];
    case TransactionState::Phase::FINISH:
      Finish(run_id);
      // Never fallthrough after this point because Finish and PreAbort
      // has already destroyed the state object
      break;
  }
}

void Worker::ReadLocalStorage(const RunId& run_id) {
  auto& state = TxnState(run_id);
  auto txn_holder = state.txn_holder;
  auto& txn = txn_holder->txn();

  if (txn.status() != TransactionStatus::ABORTED) {
#if defined(REMASTER_PROTOCOL_SIMPLE) || defined(REMASTER_PROTOCOL_PER_KEY)
    switch (RemasterManager::CheckCounters(txn, false, storage_)) {
      case VerifyMasterResult::VALID: {
        break;
      }
      case VerifyMasterResult::ABORT: {
        txn.set_status(TransactionStatus::ABORTED);
        txn.set_abort_reason("Outdated counter");
        break;
      }
      case VerifyMasterResult::WAITING: {
        LOG(FATAL) << "Transaction " << run_id << " was sent to worker with a high counter";
        break;
      }
      default:
        LOG(FATAL) << "Unrecognized check counter result";
        break;
    }
#endif

    // We don't need to check if keys are in partition here since the assumption is that
    // the out-of-partition keys have already been removed
    for (auto& [key, value] : *(txn.mutable_keys())) {
      Record record;
      if (storage_->Read(key, record)) {
        // Check whether the store master metadata matches with the information
        // stored in the transaction
        if (value.metadata().master() != record.metadata.master) {
          txn.set_status(TransactionStatus::ABORTED);
          txn.set_abort_reason("Outdated master");
          break;
        }
        value.set_value(record.to_string());
      } else if (txn.procedure_case() == Transaction::kRemaster) {
        txn.set_status(TransactionStatus::ABORTED);
        txn.set_abort_reason("Remaster non-existent key " + key);
        break;
      }
    }
  }

  BroadcastReads(run_id);

  // Set the number of remote reads that this partition needs to wait for
  state.remote_reads_waiting_on = 0;
#ifdef LOCK_MANAGER_DDR
  // If DDR is used, all partitions have to wait
  const auto& waiting_partitions = txn.internal().involved_partitions();
#else
  const auto& waiting_partitions = txn.internal().active_partitions();
#endif
  if (std::find(waiting_partitions.begin(), waiting_partitions.end(), config_->local_partition()) !=
      waiting_partitions.end()) {
    // Waiting partition needs remote reads from all partitions
    state.remote_reads_waiting_on = txn.internal().involved_partitions_size() - 1;
  }
  if (state.remote_reads_waiting_on == 0) {
    VLOG(3) << "Execute txn " << run_id << " without remote reads";
    state.phase = TransactionState::Phase::EXECUTE;
  } else {
    VLOG(3) << "Defer executing txn " << run_id << " until having enough remote reads";
    state.phase = TransactionState::Phase::WAIT_REMOTE_READ;
  }
}

void Worker::Execute(const RunId& run_id) {
  auto& state = TxnState(run_id);
  auto& txn = state.txn_holder->txn();

  switch (txn.procedure_case()) {
    case Transaction::kCode: {
      if (txn.status() == TransactionStatus::ABORTED) {
        break;
      }
      // Execute the transaction code
      commands_->Execute(txn);
      break;
    }
    case Transaction::kRemaster:
      txn.set_status(TransactionStatus::COMMITTED);
      break;
    default:
      LOG(FATAL) << "Procedure is not set";
  }
  state.phase = TransactionState::Phase::COMMIT;
}

void Worker::Commit(const RunId& run_id) {
  auto& state = TxnState(run_id);
  auto& txn = state.txn_holder->txn();
  switch (txn.procedure_case()) {
    case Transaction::kCode: {
      // Apply all writes to local storage if the transaction is not aborted
      if (txn.status() != TransactionStatus::COMMITTED) {
        VLOG(3) << "Txn " << run_id << " aborted with reason: " << txn.abort_reason();
        break;
      }
      for (const auto& [key, value] : txn.keys()) {
        if (value.type() == KeyType::READ) {
          continue;
        }
        if (config_->key_is_in_local_partition(key)) {
          Record record;
          if (!storage_->Read(key, record)) {
            record.metadata = value.metadata();
          }
          record.SetValue(value.new_value());
          storage_->Write(key, record);
        }
      }
      for (const auto& key : txn.deleted_keys()) {
        if (config_->key_is_in_local_partition(key)) {
          storage_->Delete(key);
        }
      }
      VLOG(3) << "Committed txn " << run_id;
      break;
    }
    case Transaction::kRemaster: {
      auto key_it = txn.keys().begin();
      const auto& key = key_it->first;
      if (config_->key_is_in_local_partition(key)) {
        Record record;
        storage_->Read(key, record);
        auto new_counter = key_it->second.metadata().counter() + 1;
        record.metadata = Metadata(txn.remaster().new_master(), new_counter);
        storage_->Write(key, record);

        state.txn_holder->SetRemasterResult(key, new_counter);
      }
      break;
    }
    default:
      LOG(FATAL) << "Procedure is not set";
  }
  state.phase = TransactionState::Phase::FINISH;
}

void Worker::Finish(const RunId& run_id) {
  TRACE(TxnState(run_id).txn_holder->txn().mutable_internal(), TransactionEvent::EXIT_WORKER);

  // This must happen before the sending to scheduler below. Otherwise,
  // the scheduler may destroy the transaction holder before we can
  // send the transaction to the server.
  SendToCoordinatingServer(run_id);

  // Notify the scheduler that we're done
  zmq::message_t msg(sizeof(TxnId));
  *msg.data<TxnId>() = run_id.first;
  GetCustomSocket(0).send(msg, zmq::send_flags::none);

  // Remove the redirection at broker for this txn
  auto redirect_env = NewEnvelope();
  redirect_env->mutable_request()->mutable_broker_redirect()->set_tag(MakeTag(run_id));
  redirect_env->mutable_request()->mutable_broker_redirect()->set_stop(true);
  Send(move(redirect_env), kBrokerChannel);

  // Done with this txn. Remove it from the state map
  txn_states_.erase(run_id);

  VLOG(3) << "Finished with txn " << run_id;
}

void Worker::BroadcastReads(const RunId& run_id) {
  auto& state = TxnState(run_id);
  auto txn_holder = state.txn_holder;
  auto& txn = txn_holder->txn();

#ifdef LOCK_MANAGER_DDR
  // If DDR is used, all partitions have to wait
  const auto& waiting_partitions = txn.internal().involved_partitions();
#else
  const auto& waiting_partitions = txn.internal().active_partitions();
#endif

  if (waiting_partitions.empty()) {
    return;
  }

  auto local_partition = config_->local_partition();
  auto local_replica = config_->local_replica();
  auto aborted = txn.status() == TransactionStatus::ABORTED;

  // Send abort result and local reads to all remote active partitions
  Envelope env;
  auto rrr = env.mutable_request()->mutable_remote_read_result();
  rrr->set_txn_id(run_id.first);
  rrr->set_deadlocked(run_id.second);
  rrr->set_partition(local_partition);
  rrr->set_will_abort(aborted);
  rrr->set_abort_reason(txn.abort_reason());
  if (!aborted) {
    auto reads_to_be_sent = rrr->mutable_reads();
    for (const auto& kv : txn.keys()) {
      reads_to_be_sent->insert(kv);
    }
  }

  for (auto p : waiting_partitions) {
    if (p != local_partition) {
      auto machine_id = config_->MakeMachineId(local_replica, p);
      Send(env, std::move(machine_id), MakeTag(run_id));
    }
  }
}

void Worker::SendToCoordinatingServer(const RunId& run_id) {
  auto& state = TxnState(run_id);
  auto txn_holder = state.txn_holder;

  // Send the txn back to the coordinating server
  Envelope env;
  auto completed_sub_txn = env.mutable_request()->mutable_completed_subtxn();
  completed_sub_txn->set_partition(config_->local_partition());

  auto txn = txn_holder->Release();
  if (config_->return_dummy_txn()) {
    txn->mutable_keys()->clear();
    txn->mutable_code()->clear();
    txn->mutable_remaster()->Clear();
  }
  completed_sub_txn->set_allocated_txn(txn);
  Send(env, txn->internal().coordinating_server(), kServerChannel);
}

TransactionState& Worker::TxnState(const RunId& run_id) {
  auto state_it = txn_states_.find(run_id);
  DCHECK(state_it != txn_states_.end());
  return state_it->second;
}

}  // namespace slog