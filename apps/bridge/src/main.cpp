// skyrelay-bridge: one MAVLink session per configured vehicle, published to
// DDS. Each session runs on its own thread; this thread only waits for
// SIGINT/SIGTERM and then shuts everything down in order.

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <boost/asio.hpp>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "session_runner.hpp"
#include "skyrelay/common/version.hpp"
#include "skyrelay/dds/dds_sink.hpp"
#include "skyrelay/dds/domain.hpp"

namespace {

namespace asio = boost::asio;
namespace bridge = skyrelay::bridge;
namespace mav = skyrelay::mav;
namespace vehicle = skyrelay::vehicle;
namespace sdds = skyrelay::dds;
using skyrelay::kContractVersion;
using skyrelay::kVersion;

std::string describe(const mav::LinkUri& u) {
  const char* scheme =
      u.scheme == mav::LinkScheme::UdpListen ? "udp://" : "udpout://";
  return scheme + u.host + ":" + std::to_string(u.port);
}

int run(const std::string& config_path, std::optional<uint32_t> only,
        bool allow_network) {
  const bridge::BridgeConfig cfg = bridge::load_config(config_path);

  // The channel is the vehicle's index in the file, also when --vehicle picks
  // a single one, so a vehicle keeps its channel however the fleet is split.
  std::vector<std::pair<size_t, const bridge::VehicleConfig*>> selected;
  for (size_t i = 0; i < cfg.vehicles.size(); ++i) {
    if (!only || cfg.vehicles[i].id == *only) {
      selected.emplace_back(i, &cfg.vehicles[i]);
    }
  }
  if (selected.empty()) {
    throw bridge::ConfigError("no vehicle with id " + std::to_string(*only) +
                              " (--vehicle)");
  }

  spdlog::info("skyrelay-bridge {} (contract {}), {} vehicle(s), DDS domain {} "
               "partition '{}'",
               kVersion, kContractVersion, selected.size(), cfg.dds.domain_id,
               cfg.dds.partition);
  sdds::DomainConfig dc;
  dc.domain_id = cfg.dds.domain_id;
  dc.partition = cfg.dds.partition;
  dc.allow_network = allow_network;
  if (allow_network) {
    spdlog::warn("INSECURE PREVIEW: DDS open on every network interface, "
                 "without security");
  } else {
    spdlog::warn("INSECURE PREVIEW: DDS restricted to loopback");
  }

  // Registered before any thread starts; a signal that arrives before run()
  // below is kept until then.
  asio::io_context main_io;
  asio::signal_set signals(main_io, SIGINT, SIGTERM);
  signals.async_wait([](const boost::system::error_code& ec, int sig) {
    if (!ec) spdlog::info("signal {}, shutting down", sig);
  });

  std::optional<::dds::domain::DomainParticipant> participant(
      sdds::open_participant(dc));
  auto sink = std::make_unique<sdds::DdsTelemetrySink>(*participant, dc);

  std::vector<std::unique_ptr<bridge::SessionRunner>> runners;
  for (const auto& [index, v] : selected) {
    vehicle::SessionConfig sc{};
    sc.vehicle_id = v->id;
    sc.expected_sysid = v->sysid;
    sc.gcs_sysid = cfg.gcs_sysid;
    sc.link = cfg.link;
    sc.rates = cfg.rates;
    try {
      runners.push_back(std::make_unique<bridge::SessionRunner>(
          *v, sc, static_cast<mav::Channel>(index), *sink));
    } catch (const boost::system::system_error& e) {
      throw std::runtime_error("vehicle " + std::to_string(v->id) + " (" +
                               v->name + "): cannot open " +
                               describe(v->link) + ": " + e.code().message());
    }
    spdlog::info("vehicle {} ({}): {} on channel {}, {}", v->id, v->name,
                 describe(v->link), index,
                 v->sysid ? "sysid " + std::to_string(*v->sysid)
                          : std::string("first autopilot sysid"));
  }
  for (auto& r : runners) r->start();

  main_io.run();

  // Timers and links first, then the threads; only then is nobody left
  // writing to the sink, and the participant goes last.
  const auto t0 = std::chrono::steady_clock::now();
  for (auto& r : runners) r->stop();
  runners.clear();
  sink.reset();
  participant.reset();
  spdlog::info("stopped in {} ms",
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - t0)
                   .count());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"skyrelay-bridge: MAVLink vehicles to DDS"};
  std::string config_path;
  std::optional<uint32_t> only;
  bool allow_network = false;
  app.add_option("--config", config_path, "Bridge configuration (YAML)")
      ->required();
  app.add_option("--vehicle", only,
                 "Run only the vehicle with this id (one process per vehicle)");
  app.add_flag("--insecure-allow-network", allow_network,
               "Let DDS use non-loopback interfaces. There is no DDS "
               "security in this preview.");
  CLI11_PARSE(app, argc, argv);

  try {
    return run(config_path, only, allow_network);
  } catch (const bridge::ConfigError& e) {
    spdlog::error("{}: {}", config_path, e.what());
    return 2;
  } catch (const std::exception& e) {
    spdlog::error("{}", e.what());
    return 1;
  }
}
