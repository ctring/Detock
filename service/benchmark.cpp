#include <algorithm>
#include <chrono>
#include <iomanip>
#include <optional>
#include <random>
#include <unordered_map>

#include "common/configuration.h"
#include "common/csv_writer.h"
#include "common/json_utils.h"
#include "common/proto_utils.h"
#include "common/string_utils.h"
#include "module/txn_generator.h"
#include "service/service_utils.h"
#include "workload/basic_workload.h"
#include "workload/remastering_workload.h"

DEFINE_string(config, "slog.conf", "Path to the configuration file");
DEFINE_int32(workers, 1, "Number of worker threads");
DEFINE_uint32(r, 0, "The region where the current machine is located");
DEFINE_string(data_dir, "", "Directory containing intial data");
DEFINE_string(out_dir, "", "Directory containing output data");
DEFINE_int32(rate, 0, "Maximum number of transactions sent per second.");
DEFINE_int32(clients, 0, "Number of concurrent client. This option does nothing if 'rate' is set");
DEFINE_int32(duration, 0, "Maximum duration in seconds to run the benchmark");
DEFINE_uint32(txns, 100, "Total number of txns to be generated");
DEFINE_string(wl, "basic", "Name of the workload to use (options: basic, remastering)");
DEFINE_string(params, "", "Parameters of the workload");
DEFINE_bool(dry_run, false, "Generate the transactions without actually sending to the server");
DEFINE_double(sample, 10, "Percent of sampled transactions to be written to result files");
DEFINE_int32(
    seed, -1,
    "Seed for any randomization in the benchmark. If set to negative, seed will be picked from std::random_device()");
DEFINE_bool(txn_profiles, false, "Output transaction profiles");

using namespace slog;

using std::count_if;
using std::make_unique;
using std::setw;
using std::unique_ptr;

uint32_t seed = std::random_device{}();
zmq::context_t context;

vector<unique_ptr<ModuleRunner>> InitializeWorkers() {
  // Load the config
  auto config = Configuration::FromFile(FLAGS_config, "");

  // Setup zmq context
  context.set(zmq::ctxopt::blocky, false);

  // Initialize the workers
  FLAGS_workers = std::max(FLAGS_workers, 1);
  auto remaining_txns = FLAGS_txns;
  auto num_txns_per_worker = FLAGS_txns / FLAGS_workers;
  vector<std::unique_ptr<ModuleRunner>> workers;
  for (int i = 0; i < FLAGS_workers; i++) {
    // Select the workload
    unique_ptr<Workload> workload;
    if (FLAGS_wl == "basic") {
      workload = make_unique<BasicWorkload>(config, FLAGS_r, FLAGS_data_dir, FLAGS_params, seed + i);
    } else if (FLAGS_wl == "remastering") {
      workload = make_unique<RemasteringWorkload>(config, FLAGS_r, FLAGS_data_dir, FLAGS_params, seed + i);
    } else {
      LOG(FATAL) << "Unknown workload: " << FLAGS_wl;
    }
    if (i < FLAGS_workers - 1) {
      remaining_txns -= num_txns_per_worker;
    } else {
      num_txns_per_worker = remaining_txns;
    }
    if (FLAGS_rate > 0) {
      auto tps_per_worker = FLAGS_rate / FLAGS_workers + (i < (FLAGS_rate % FLAGS_workers));
      workers.push_back(MakeRunnerFor<ConstantRateTxnGenerator>(config, context, std::move(workload), FLAGS_r,
                                                                num_txns_per_worker, tps_per_worker, FLAGS_duration,
                                                                FLAGS_dry_run));
    } else {
      int num_clients = FLAGS_clients / FLAGS_workers + (i < (FLAGS_clients % FLAGS_workers));
      workers.push_back(MakeRunnerFor<SynchronousTxnGenerator>(config, context, std::move(workload), FLAGS_r,
                                                               num_txns_per_worker, num_clients, FLAGS_duration,
                                                               FLAGS_dry_run));
    }
  }
  return workers;
}

void RunBenchmark(vector<unique_ptr<ModuleRunner>>& workers) {
  // Block SIGINT from here so that the new threads inherit the block mask
  sigset_t signal_set;
  sigemptyset(&signal_set);
  sigaddset(&signal_set, SIGINT);
  pthread_sigmask(SIG_BLOCK, &signal_set, nullptr);

  // Run the workers
  for (auto& w : workers) {
    w->StartInNewThread();
  }

  // Wait until all workers finish the setting up phase
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    bool setup = true;
    for (const auto& w : workers) setup &= w->set_up();
    if (setup) break;
  }

  // Status report until all workers finish running
  size_t last_num_sent_txns = 0;
  size_t last_num_recv_txns = 0;
  auto last_print_time = std::chrono::steady_clock::now();
  timespec sigpoll_time = {.tv_sec = 0, .tv_nsec = 0};
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    bool running = false;
    size_t num_sent_txns = 0;
    size_t num_recv_txns = 0;
    for (auto& w : workers) {
      running |= w->is_running();
      auto gen = dynamic_cast<const TxnGenerator*>(w->module().get());
      num_sent_txns += gen->num_sent_txns();
      num_recv_txns += gen->num_recv_txns();
    }
    auto now = std::chrono::steady_clock::now();
    auto t = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_print_time);
    auto send_tps = (num_sent_txns - last_num_sent_txns) * 1000 / t.count();
    auto recv_tps = (num_recv_txns - last_num_recv_txns) * 1000 / t.count();

    // Effectively skip the first log since it is usually inaccurate.
    if (last_num_sent_txns > 0) {
      LOG(INFO) << "Sent: " << num_sent_txns << "; Received: " << num_recv_txns << "; Sent tps: " << send_tps
                << "; Recv tps: " << recv_tps << "\n";
    }

    last_num_sent_txns = num_sent_txns;
    last_num_recv_txns = num_recv_txns;
    last_print_time = now;

    if (!running) {
      break;
    }

    if (sigtimedwait(&signal_set, nullptr, &sigpoll_time) >= 0) {
      LOG(WARNING) << "Benchmark interuptted. Partial results collected.";
      break;
    }
  }
}

struct ResultWriters {
  const vector<string> kTxnColumns = {"txn_id", "coordinator", "replicas", "partitions", "sent_at", "received_at"};
  const vector<string> kEventsColumns = {"txn_id", "event", "time", "machine"};
  const vector<string> kSummaryColumns = {"avg_tps",    "aborted",  "committed",   "single_home",
                                          "multi_home", "remaster", "elapsed_time"};

  ResultWriters()
      : txns(FLAGS_out_dir + "/transactions.csv", kTxnColumns),
        events(FLAGS_out_dir + "/txn_events.csv", kEventsColumns),
        summary(FLAGS_out_dir + "/summary.csv", kSummaryColumns) {}

  CSVWriter txns;
  CSVWriter events;
  CSVWriter summary;
};
std::optional<ResultWriters> writers;

void WriteResults(const vector<unique_ptr<ModuleRunner>>& workers) {
  if (!writers.has_value()) {
    return;
  }

  // Write metadata
  rapidjson::Document metadata(rapidjson::kObjectType);
  auto& alloc = metadata.GetAllocator();

  metadata.AddMember("duration", FLAGS_duration, alloc);
  metadata.AddMember("txns", FLAGS_txns, alloc);
  metadata.AddMember("sample", FLAGS_sample, alloc);
  if (FLAGS_rate > 0) {
    metadata.AddMember("rate", FLAGS_rate, alloc);
  } else {
    metadata.AddMember("clients", FLAGS_clients, alloc);
  }
  CHECK(!workers.empty());
  auto worker = dynamic_cast<const TxnGenerator*>(workers.front()->module().get());
  auto workload = worker->workload().params().as_json(alloc);
  workload.AddMember("name", rapidjson::Value(worker->workload().name().c_str(), alloc), alloc);
  metadata.AddMember("workload", workload, alloc);

  rapidjson::StringBuffer metadata_buf;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> metadata_writer(metadata_buf);
  metadata.Accept(metadata_writer);
  auto metadata_filename = FLAGS_out_dir + "/metadata.json";
  std::ofstream metadata_file(metadata_filename, std::ios::out);
  if (!metadata_file) {
    throw std::runtime_error(std::string("Cannot open file: ") + metadata_filename);
  }
  metadata_file << metadata_buf.GetString();

  // Aggregate complete data and output summary
  for (auto& w : workers) {
    float avg_tps = 0;
    int aborted = 0, committed = 0, not_started = 0, single_home = 0, multi_home = 0, remaster = 0;
    auto worker = dynamic_cast<const TxnGenerator*>(w->module().get());
    const auto& txn_infos = worker->txn_infos();
    for (auto info : txn_infos) {
      committed += info.txn->status() == TransactionStatus::COMMITTED;
      aborted += info.txn->status() == TransactionStatus::ABORTED;
      not_started += info.txn->status() == TransactionStatus::NOT_STARTED;
      single_home += info.txn->internal().type() == TransactionType::SINGLE_HOME;
      multi_home += info.txn->internal().type() == TransactionType::MULTI_HOME_OR_LOCK_ONLY;
      remaster += info.txn->procedure_case() == Transaction::ProcedureCase::kRemaster;
    }
    avg_tps += committed * 1000 / std::chrono::duration_cast<std::chrono::milliseconds>(worker->elapsed_time()).count();
    writers->summary << avg_tps << aborted << committed << single_home << multi_home << remaster
                     << worker->elapsed_time().count() << csvendl;
  }

  // Sample a subset of the result
  vector<TxnGenerator::TxnInfo> txn_infos;
  for (auto& w : workers) {
    auto gen = dynamic_cast<const TxnGenerator*>(w->module().get());
    txn_infos.insert(txn_infos.end(), gen->txn_infos().begin(), gen->txn_infos().end());
  }
  std::mt19937 rg(seed);
  std::shuffle(txn_infos.begin(), txn_infos.end(), rg);
  auto sample_size = static_cast<size_t>(txn_infos.size() * FLAGS_sample / 100);
  txn_infos.resize(sample_size);

  for (const auto& info : txn_infos) {
    CHECK(info.txn != nullptr);
    auto& txn_internal = info.txn->internal();
    writers->txns << txn_internal.id() << txn_internal.coordinating_server() << Join(txn_internal.involved_replicas())
                  << Join(txn_internal.involved_partitions()) << info.sent_at.time_since_epoch().count()
                  << info.recv_at.time_since_epoch().count() << csvendl;

    for (int i = 0; i < txn_internal.events_size(); i++) {
      auto event = txn_internal.events(i);
      writers->events << txn_internal.id() << ENUM_NAME(event, TransactionEvent) << txn_internal.event_times(i)
                      << txn_internal.event_machines(i) << csvendl;
    }
  }

  if (FLAGS_txn_profiles) {
    auto file_name = FLAGS_out_dir + "/txn_profiles.txt";
    std::ofstream profiles(file_name, std::ios::out);
    if (!profiles) {
      throw std::runtime_error(std::string("Cannot open file: ") + file_name);
    }
    const int kCellWidth = 12;
    for (const auto& info : txn_infos) {
      profiles << *info.txn;
      profiles << "Multi-Home: " << info.profile.is_multi_home << "\n";
      profiles << "Multi-Partition: " << info.profile.is_multi_partition << "\n";
      profiles << "Profile:\n";
      profiles << setw(kCellWidth) << "Key" << setw(kCellWidth) << "Home" << setw(kCellWidth) << "Partition"
               << setw(kCellWidth) << "Hot" << setw(kCellWidth) << "Write"
               << "\n";
      for (const auto& [key, record] : info.profile.records) {
        profiles << setw(kCellWidth) << key << setw(kCellWidth) << record.home << setw(kCellWidth) << record.partition
                 << setw(kCellWidth) << record.is_hot << setw(kCellWidth) << record.is_write << "\n";
      }
      profiles << "\n" << std::endl;
    }
  }

  LOG(INFO) << "Results were written to \"" << FLAGS_out_dir << "/\"";
}

int main(int argc, char* argv[]) {
  InitializeService(&argc, &argv);

  if (FLAGS_seed >= 0) {
    seed = FLAGS_seed;
  }

  if (FLAGS_dry_run) {
    LOG(WARNING) << "Generating transactions without sending to servers";
  }

  CHECK(FLAGS_clients > 0 || FLAGS_rate > 0) << "Either 'clients' or 'rate' must be set";
  if (FLAGS_clients > 0 && FLAGS_rate > 0) {
    LOG(WARNING) << "The 'rate' flag is set, the 'client' flag will be ignored";
  }

  LOG(INFO) << "Arguments:\n"
            << "Workload: " << FLAGS_wl << "\nParams: " << FLAGS_params << "\nNum txns: " << FLAGS_txns
            << "\nSending rate: " << FLAGS_rate << "\nNum clients: " << FLAGS_clients
            << "\nDuration: " << FLAGS_duration;

  if (FLAGS_out_dir.empty()) {
    LOG(WARNING) << "Results will not be written to files because output directory is not provided";
  } else {
    LOG(INFO) << "Results will be written to \"" << FLAGS_out_dir << "/\"";
    writers.emplace();
  }

  auto workers = InitializeWorkers();

  RunBenchmark(workers);

  WriteResults(workers);

  float avg_tps = 0;
  int aborted = 0, committed = 0, not_started = 0, single_home = 0, multi_home = 0, remaster = 0;
  for (auto& w : workers) {
    auto worker = dynamic_cast<const TxnGenerator*>(w->module().get());
    const auto& txn_infos = worker->txn_infos();
    int worker_committed = 0;
    for (auto info : txn_infos) {
      worker_committed += info.txn->status() == TransactionStatus::COMMITTED;
      aborted += info.txn->status() == TransactionStatus::ABORTED;
      not_started += info.txn->status() == TransactionStatus::NOT_STARTED;
      single_home += info.txn->internal().type() == TransactionType::SINGLE_HOME;
      multi_home += info.txn->internal().type() == TransactionType::MULTI_HOME_OR_LOCK_ONLY;
      remaster += info.txn->procedure_case() == Transaction::ProcedureCase::kRemaster;
    }
    avg_tps += worker_committed * 1000 / std::chrono::duration_cast<std::chrono::milliseconds>(worker->elapsed_time()).count();
    committed += worker_committed;
  }

  LOG(INFO) << "Summary:\n"
            << "Avg. TPS: " << std::floor(avg_tps) << "\nAborted: " << aborted << "\nCommitted: " << committed
            << "\nNot started: " << not_started << "\nSingle-home: " << single_home << "\nMulti-home: " << multi_home
            << "\nRemaster: " << remaster;

  return 0;
}