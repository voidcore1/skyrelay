// skyrelay-bench-probe: a plain DDS subscriber for the benchmark.
//
//   --duration S --out FILE   timestamp every pose sample, write a CSV
//   --wait-connected N        exit 0 once N vehicles report LINK_CONNECTED
//
// The receive time is taken with CLOCK_REALTIME inside on_data_available, the
// same clock the bridge uses for t_bridge_ns, so the difference is the time
// from the bridge's decode to the consumer. Rows are kept in memory and the
// file is written only after the run, so I/O never delays a timestamp.

#include <CLI/CLI.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <exception>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "skyrelay/dds/domain.hpp"
#include "skyrelay/dds/idl.hpp"
#include "skyrelay/dds/qos.hpp"
#include "skyrelay/dds/topics.hpp"

namespace {

namespace sdds = skyrelay::dds;
namespace v1 = skyrelay::v1;

std::atomic<bool> g_stop{false};
extern "C" void on_signal(int) { g_stop.store(true); }

uint64_t realtime_ns() {
  timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

struct Row {
  uint32_t vehicle_id;
  uint64_t seq;
  uint64_t t_source_us;
  uint64_t t_bridge_ns;
  uint64_t t_probe_ns;
};

class PoseListener : public ::dds::sub::NoOpDataReaderListener<v1::Pose> {
 public:
  explicit PoseListener(size_t reserve) { rows_.reserve(reserve); }

  void on_data_available(::dds::sub::DataReader<v1::Pose>& reader) override {
    const uint64_t now = realtime_ns();
    auto samples = reader.take();
    for (const auto& s : samples) {
      if (!s.info().valid()) continue;
      const v1::Pose& p = s.data();
      rows_.push_back({p.vehicle_id(), p.header().seq(), p.header().t_source_us(),
                       p.header().t_bridge_ns(), now});
    }
  }

  const std::vector<Row>& rows() const { return rows_; }

 private:
  // Only the DDS receive thread appends; the main thread reads after the
  // reader is destroyed, so no lock is needed.
  std::vector<Row> rows_;
};

int record(const sdds::DomainConfig& dc, double duration_s,
           const std::string& out) {
  auto dp = sdds::open_participant(dc);
  auto sub = sdds::make_subscriber(dp, dc);
  const std::string name(sdds::kTopicPose);
  ::dds::topic::Topic<v1::Pose> topic(
      dp, name, sdds::topic_qos(sdds::profile_for(name)));
  // 4 vehicles at 20 Hz is 80 rows/s; reserve for 8x that so a normal run
  // never reallocates inside the callback.
  const size_t reserve = static_cast<size_t>(duration_s * 640.0) + 1024;
  PoseListener listener(reserve);
  size_t count = 0;
  {
    ::dds::sub::DataReader<v1::Pose> reader(
        sub, topic, sdds::reader_qos(sdds::profile_for(name)), &listener,
        ::dds::core::status::StatusMask::data_available());
    const auto end = std::chrono::steady_clock::now() +
                     std::chrono::duration<double>(duration_s);
    while (!g_stop.load() && std::chrono::steady_clock::now() < end) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // Detach the listener before the reader goes, so no callback is running
    // while the rows are read below.
    reader.listener(nullptr, ::dds::core::status::StatusMask::none());
  }
  count = listener.rows().size();

  std::FILE* f = std::fopen(out.c_str(), "w");
  if (!f) {
    std::fprintf(stderr, "cannot write %s\n", out.c_str());
    return 1;
  }
  std::fprintf(f, "vehicle_id,seq,t_source_us,t_bridge_ns,t_probe_ns\n");
  for (const Row& r : listener.rows()) {
    std::fprintf(f, "%u,%llu,%llu,%llu,%llu\n", r.vehicle_id,
                 static_cast<unsigned long long>(r.seq),
                 static_cast<unsigned long long>(r.t_source_us),
                 static_cast<unsigned long long>(r.t_bridge_ns),
                 static_cast<unsigned long long>(r.t_probe_ns));
  }
  std::fclose(f);
  std::printf("%zu pose samples written to %s%s\n", count, out.c_str(),
              g_stop.load() ? " (interrupted)" : "");
  return 0;
}

int wait_connected(const sdds::DomainConfig& dc, uint32_t want,
                   double timeout_s) {
  auto dp = sdds::open_participant(dc);
  auto sub = sdds::make_subscriber(dp, dc);
  const std::string name(sdds::kTopicLinkStatus);
  ::dds::topic::Topic<v1::LinkStatus> topic(
      dp, name, sdds::topic_qos(sdds::profile_for(name)));
  ::dds::sub::DataReader<v1::LinkStatus> reader(
      sub, topic, sdds::reader_qos(sdds::profile_for(name)));

  std::map<uint32_t, v1::LinkState> latest;
  auto connected = [&] {
    uint32_t n = 0;
    for (const auto& [id, st] : latest) {
      if (st == v1::LinkState::LINK_CONNECTED) ++n;
    }
    return n;
  };
  const auto end = std::chrono::steady_clock::now() +
                   std::chrono::duration<double>(timeout_s);
  while (!g_stop.load() && std::chrono::steady_clock::now() < end) {
    for (const auto& s : reader.take()) {
      if (!s.info().valid()) continue;
      latest[s.data().vehicle_id()] = s.data().state();
    }
    if (connected() >= want) {
      std::printf("%u vehicle(s) connected\n", connected());
      return 0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::fprintf(stderr, "only %u of %u vehicle(s) connected; saw %zu:", connected(),
               want, latest.size());
  for (const auto& [id, st] : latest) {
    std::fprintf(stderr, " %u=%s", id,
                 st == v1::LinkState::LINK_CONNECTED    ? "CONNECTED"
                 : st == v1::LinkState::LINK_DEGRADED   ? "DEGRADED"
                 : st == v1::LinkState::LINK_RECOVERING ? "RECOVERING"
                                                         : "LOST");
  }
  std::fprintf(stderr, "%s\n", latest.empty() ? " nothing" : "");
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"skyrelay-bench-probe: timestamps pose samples for the latency benchmark"};
  double duration = 60;
  std::string out = "pose.csv";
  uint32_t domain = 42;
  std::string partition = "default";
  uint32_t wait_n = 0;
  double timeout = 120;
  app.add_option("--duration", duration, "Seconds to record pose samples");
  app.add_option("--out", out, "CSV output file");
  app.add_option("--domain", domain, "DDS domain id");
  app.add_option("--partition", partition, "DDS data partition");
  auto* wc = app.add_option("--wait-connected", wait_n,
                            "Wait until N vehicles report LINK_CONNECTED");
  app.add_option("--timeout", timeout, "Seconds to wait for --wait-connected")
      ->needs(wc);
  CLI11_PARSE(app, argc, argv);

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  sdds::DomainConfig dc;
  dc.domain_id = domain;
  dc.partition = partition;
  try {
    return wc->count() ? wait_connected(dc, wait_n, timeout)
                       : record(dc, duration, out);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "skyrelay-bench-probe: %s\n", e.what());
    return 1;
  }
}
