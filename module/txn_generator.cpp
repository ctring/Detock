#include "module/txn_generator.h"

#include <glog/logging.h>

#include <sstream>

#include "common/constants.h"
#include "connection/zmq_utils.h"
#include "proto/api.pb.h"

using std::shared_ptr;
using std::unique_ptr;
using std::chrono::system_clock;

namespace slog {
namespace {
void ConnectToServer(const ConfigurationPtr& config, zmq::socket_t& socket, uint32_t region) {
  socket.set(zmq::sockopt::sndhwm, 0);
  socket.set(zmq::sockopt::rcvhwm, 0);
  for (uint32_t p = 0; p < config->num_partitions(); p++) {
    std::ostringstream endpoint_s;
    if (config->protocol() == "ipc") {
      endpoint_s << "tcp://localhost:" << config->server_port();
    } else {
      endpoint_s << "tcp://" << config->address(region, p) << ":" << config->server_port();
    }
    auto endpoint = endpoint_s.str();
    LOG(INFO) << "Connecting to " << endpoint;
    socket.connect(endpoint);
  }
}

bool RecordFinishedTxn(TxnGenerator::TxnInfo& info, Transaction* txn, bool is_dummy) {
  if (info.finished) {
    LOG(ERROR) << "Received response for finished txn";
    return false;
  } 
  info.recv_at = system_clock::now();
  if (is_dummy) {
    if (info.txn == nullptr) {
      LOG(ERROR) << "No transaction in the txn info";
    } else {
      info.txn->set_status(txn->status());
      info.txn->mutable_internal()->CopyFrom(txn->internal());
      delete txn;
    }
  } else {
    delete info.txn;
    info.txn = txn;
  }
  info.finished = true;
  return true;
}

}  // namespace


SynchronizedTxnGenerator::SynchronizedTxnGenerator(const ConfigurationPtr& config, zmq::context_t& context,
                                                   std::unique_ptr<Workload>&& workload, uint32_t region,
                                                   uint32_t num_txns, int num_clients, int duration_s,
                                                   bool dry_run)
    : Module("Synchronized-Txn-Generator"),
      config_(config),
      socket_(context, ZMQ_DEALER),
      workload_(std::move(workload)),
      poller_(kModuleTimeout),
      region_(region),
      num_txns_(num_txns),
      num_clients_(num_clients),
      duration_(duration_s * 1000),
      dry_run_(dry_run),
      cur_txn_(0),
      num_recv_txns_(0) {
  CHECK(workload_ != nullptr) << "Must provide a valid workload";
}

SynchronizedTxnGenerator::~SynchronizedTxnGenerator() {
  for (auto& txn : generated_txns_) {
    delete txn.first;
    txn.first = nullptr;
  }
  for (auto& txn : txns_) {
    delete txn.txn;
    txn.txn = nullptr;
  }
}

void SynchronizedTxnGenerator::SetUp() {
  CHECK_LT(num_txns_, 0) << "There must be at least one transaction";
  LOG(INFO) << "Generating " << num_txns_ << " transactions";
  for (size_t i = 0; i < num_txns_; i++) {
    generated_txns_.push_back(workload_->NextTransaction());
  }

  if (!dry_run_) {
    ConnectToServer(config_, socket_, region_);
    poller_.PushSocket(socket_);
    for (int i = 0; i < num_clients_; i++) {
      SendNextTxn();
    }
  }

  start_time_ = steady_clock::now();
}

bool SynchronizedTxnGenerator::Loop() {
  if (dry_run_) {
    return false;
  }

  bool duration_reached = steady_clock::now() - start_time_ >= duration_;
  if (poller_.NextEvent()) {
    if (api::Response res; RecvDeserializedProtoWithEmptyDelim(socket_, res)) {
      auto& info = txns_[res.stream_id()];
      if (RecordFinishedTxn(info, res.mutable_txn()->release_txn(), config_->return_dummy_txn())) {
        num_recv_txns_++;
        if (!duration_reached) {
          SendNextTxn();
        }
      }
    }
  }

  if (duration_reached && num_recv_txns_ == txns_.size()) {
    elapsed_time_ = duration_cast<milliseconds>(steady_clock::now() - start_time_);
    return true;
  }
  return false;
}

void SynchronizedTxnGenerator::SendNextTxn() {
  const auto& selected_txn = generated_txns_[cur_txn_ % generated_txns_.size()];

  api::Request req;
  req.mutable_txn()->set_allocated_txn(new Transaction(*selected_txn.first));
  req.set_stream_id(cur_txn_);
  SendSerializedProtoWithEmptyDelim(socket_, req);

  TxnInfo info;
  info.txn = req.mutable_txn()->release_txn();
  info.profile = selected_txn.second;
  info.sent_at = system_clock::now();
  txns_.push_back(std::move(info));

  ++cur_txn_;
}

ConstantRateTxnGenerator::ConstantRateTxnGenerator(const ConfigurationPtr& config, zmq::context_t& context,
                                                   unique_ptr<Workload>&& workload, uint32_t region, int num_txns,
                                                   int tps, bool dry_run)
    : Module("Txn-Generator"),
      config_(config),
      socket_(context, ZMQ_DEALER),
      workload_(std::move(workload)),
      poller_(kModuleTimeout),
      region_(region),
      num_txns_(num_txns),
      dry_run_(dry_run),
      cur_txn_(0),
      num_recv_txns_(0) {
  CHECK(workload_ != nullptr) << "Must provide a valid workload";

  CHECK_LT(tps, 1000000) << "Transaction/sec is too high (max. 1000000)";
  uint32_t overhead_estimate = !dry_run_ * 10;
  if (1000000 / tps > overhead_estimate) {
    interval_ = std::chrono::microseconds(1000000 / tps - overhead_estimate);
  } else {
    interval_ = 0us;
  }
}

ConstantRateTxnGenerator::~ConstantRateTxnGenerator() {
  for (auto& txn : txns_) {
    delete txn.txn;
    txn.txn = nullptr;
  }
}

void ConstantRateTxnGenerator::SetUp() {
  LOG(INFO) << "Generating " << num_txns_ << " transactions";
  for (size_t i = 0; i < num_txns_; i++) {
    auto new_txn = workload_->NextTransaction();
    TxnInfo info{
        .txn = new_txn.first,
        .profile = new_txn.second,
        .sent_at = system_clock::now(),
        .recv_at = system_clock::now(),
        .finished = false,
    };
    txns_.push_back(std::move(info));
  }

  if (!dry_run_) {
    ConnectToServer(config_, socket_, region_);
    poller_.PushSocket(socket_);
  }

  // Schedule sending new txns
  poller_.AddTimedCallback(interval_, [this]() { SendNextTxn(); });

  start_time_ = steady_clock::now();
}

void ConstantRateTxnGenerator::SendNextTxn() {
  if (cur_txn_ >= txns_.size()) {
    return;
  }
  auto& info = txns_[cur_txn_];

  // Send current txn
  if (!dry_run_) {
    api::Request req;
    req.mutable_txn()->set_allocated_txn(info.txn);
    req.set_stream_id(cur_txn_);
    SendSerializedProtoWithEmptyDelim(socket_, req);
    info.txn = req.mutable_txn()->release_txn();
  }
  info.sent_at = system_clock::now();

  // Schedule for next txn
  ++cur_txn_;
  poller_.AddTimedCallback(interval_, [this]() { SendNextTxn(); });
}

bool ConstantRateTxnGenerator::Loop() {
  if (poller_.NextEvent()) {
    if (api::Response res; RecvDeserializedProtoWithEmptyDelim(socket_, res)) {
      auto& info = txns_[res.stream_id()];
      num_recv_txns_ += RecordFinishedTxn(info, res.mutable_txn()->release_txn(), config_->return_dummy_txn());
    }
  }

  if (num_recv_txns_ == txns_.size() || (dry_run_ && cur_txn_ == txns_.size())) {
    elapsed_time_ = duration_cast<milliseconds>(steady_clock::now() - start_time_);
    return true;
  }
  return false;
}

}  // namespace slog