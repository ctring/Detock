#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "common/types.h"
#include "proto/configuration.pb.h"
#include "proto/internal.pb.h"

namespace slog {

class Configuration;

using ConfigurationPtr = std::shared_ptr<const Configuration>;

class Configuration {
 public:
  static ConfigurationPtr FromFile(const std::string& file_path, const std::string& local_address = "");

  Configuration(const internal::Configuration& config, const std::string& local_address);

  const std::string& protocol() const;
  const std::vector<std::string>& all_addresses() const;
  const std::string& address(uint32_t replica, uint32_t partition) const;
  const std::string& address(MachineId machine_id) const;
  uint32_t broker_ports(int i) const;
  uint32_t broker_ports_size() const;
  uint32_t server_port() const;
  uint32_t forwarder_port() const;
  uint32_t sequencer_port() const;
  uint32_t clock_synchronizer_port() const;
  uint32_t num_replicas() const;
  uint32_t num_partitions() const;
  uint32_t num_workers() const;
  std::vector<MachineId> all_machine_ids() const;
  std::chrono::milliseconds forwarder_batch_duration() const;
  std::chrono::milliseconds sequencer_batch_duration() const;
  uint32_t replication_factor() const;

  const std::string& local_address() const;
  uint32_t local_replica() const;
  uint32_t local_partition() const;
  MachineId local_machine_id() const;
  MachineId MakeMachineId(uint32_t replica, uint32_t partition) const;
  std::pair<uint32_t, uint32_t> UnpackMachineId(MachineId machine_id) const;

  uint32_t leader_replica_for_multi_home_ordering() const;
  uint32_t leader_partition_for_multi_home_ordering() const;

  uint32_t partition_of_key(const Key& key) const;
  bool key_is_in_local_partition(const Key& key) const;
  // Only work with simple partitioning
  int partition_of_key(uint32_t key) const;
  uint32_t master_of_key(uint32_t key) const;
  const internal::SimplePartitioning* simple_partitioning() const;

  uint32_t replication_delay_pct() const;
  uint32_t replication_delay_amount_ms() const;

  std::vector<TransactionEvent> enabled_events() const;
  bool bypass_mh_orderer() const;
  std::chrono::milliseconds ddr_interval() const;
  std::vector<int> cpu_pinnings(ModuleId module) const;
  bool return_dummy_txn() const;
  int recv_retries() const;
  internal::ExecutionType execution_type() const;
  const std::vector<uint32_t> replication_order() const;
  bool synchronized_batching() const;
  const internal::MetricOptions& metric_options() const;
  std::array<int, 2> interleaver_remote_to_local_ratio() const;
  std::chrono::milliseconds fs_latency_interval() const;
  std::chrono::milliseconds clock_sync_interval() const;
  int64_t timestamp_buffer_us() const;
  uint32_t avg_latency_window_size() const;

 private:
  internal::Configuration config_;
  std::string local_address_;
  int local_replica_;
  int local_partition_;

  std::vector<std::string> all_addresses_;
  std::vector<uint32_t> replication_order_;
};

}  // namespace slog