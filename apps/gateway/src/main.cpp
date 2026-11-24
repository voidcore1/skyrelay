// skyrelay-gateway: serves the web station, its offline map data and a
// WebSocket that streams DDS telemetry as JSON.
//
// The HTTP/WebSocket server runs on one io_context thread. DDS listener
// callbacks build each sample's JSON on Cyclone's thread and post it to that
// thread, which owns the fan-out. This thread only waits for SIGINT/SIGTERM
// and then shuts down in order.

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <boost/asio.hpp>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "dds_feed.hpp"
#include "skyrelay/common/clock.hpp"
#include "skyrelay/common/version.hpp"
#include "skyrelay/dds/domain.hpp"
#include "ws_server.hpp"

namespace {

namespace asio = boost::asio;
namespace fs = std::filesystem;
namespace gw = skyrelay::gateway;
namespace sdds = skyrelay::dds;
using skyrelay::kContractVersion;
using skyrelay::kVersion;

// Shutdown has to finish within a second. The connections get this long to
// close before the io_context is stopped regardless.
constexpr auto kCloseBudget = std::chrono::milliseconds(700);

struct Options {
  asio::ip::tcp::endpoint listen;
  std::string web_root;
  std::string data_dir;
  sdds::DomainConfig dds;
};

std::string describe(const asio::ip::tcp::endpoint& ep) {
  const auto a = ep.address();
  return (a.is_v6() ? "[" + a.to_string() + "]" : a.to_string()) + ":" +
         std::to_string(ep.port());
}

// A handler should never throw; if one does, log it and keep serving.
void run_io(asio::io_context& io) {
  for (;;) {
    try {
      io.run();
      return;
    } catch (const std::exception& e) {
      spdlog::error("io thread: {}", e.what());
    }
  }
}

int run(const Options& o) {
  spdlog::info("skyrelay-gateway {} (contract {}), DDS domain {} partition '{}'",
               kVersion, kContractVersion, o.dds.domain_id, o.dds.partition);
  if (o.dds.allow_network) {
    spdlog::warn("INSECURE PREVIEW: DDS open on every network interface, "
                 "without security");
  } else {
    spdlog::warn("INSECURE PREVIEW: DDS restricted to loopback");
  }
  spdlog::warn("INSECURE PREVIEW: HTTP and WebSocket without TLS or "
               "authentication, loopback only");
  if (!fs::is_directory(o.web_root)) {
    spdlog::warn("web root {} is not a directory; the station will answer 404",
                 o.web_root);
  }
  if (!fs::is_directory(o.data_dir)) {
    spdlog::warn("data dir {} is not a directory; map tiles and assets will answer 404",
                 o.data_dir);
  }

  // Registered before any thread starts; a signal that arrives before run()
  // below is kept until then.
  asio::io_context main_io;
  asio::signal_set signals(main_io, SIGINT, SIGTERM);
  signals.async_wait([](const boost::system::error_code& ec, int sig) {
    if (!ec) spdlog::info("signal {}, shutting down", sig);
  });

  asio::io_context io;
  skyrelay::SystemClock clock;
  gw::ServerConfig cfg;
  cfg.listen = o.listen;
  cfg.web_root = o.web_root;
  cfg.data_dir = o.data_dir;
  cfg.latched_topics = gw::DdsFeed::latched_topics();
  gw::WsServer server(io, std::move(cfg), clock);
  server.start();

  std::optional<::dds::domain::DomainParticipant> participant(
      sdds::open_participant(o.dds));
  auto feed = std::make_unique<gw::DdsFeed>(
      *participant, o.dds,
      [&server](std::string_view topic, uint32_t vehicle_id, std::string json,
                uint64_t t_bridge_ns) {
        server.post_sample(topic, vehicle_id, std::move(json), t_bridge_ns);
      });

  std::promise<void> io_done;
  auto io_finished = io_done.get_future();
  std::thread io_thread([&io, &io_done] {
    run_io(io);
    io_done.set_value();
  });
  spdlog::info("listening on http://{} (web root {}, data dir {})",
               describe(server.local_endpoint()), o.web_root, o.data_dir);

  main_io.run();

  // No new samples first, then the connections, then DDS.
  const auto t0 = std::chrono::steady_clock::now();
  feed.reset();
  server.stop();
  if (io_finished.wait_for(kCloseBudget) != std::future_status::ready) {
    spdlog::warn("connections still closing after {} ms, stopping anyway",
                 kCloseBudget.count());
    io.stop();
  }
  io_thread.join();
  participant.reset();
  spdlog::info("stopped in {} ms",
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - t0)
                   .count());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"skyrelay-gateway: web station, offline map data and telemetry WebSocket"};
  std::string listen = "127.0.0.1:8080";
  Options o;
  o.web_root = "web/station/dist";
  o.data_dir = "data";
  app.add_option("--listen", listen,
                 "HTTP/WebSocket address as IP:port; loopback only in this preview")
      ->capture_default_str();
  app.add_option("--web-root", o.web_root, "Built web station to serve")
      ->capture_default_str();
  app.add_option("--data-dir", o.data_dir, "Map data, with tiles/ and assets/")
      ->capture_default_str();
  app.add_option("--domain", o.dds.domain_id, "DDS domain id")->capture_default_str();
  app.add_option("--partition", o.dds.partition, "DDS data partition")
      ->capture_default_str();
  app.add_flag("--insecure-allow-network", o.dds.allow_network,
               "Let DDS use non-loopback interfaces. There is no DDS "
               "security in this preview.");
  CLI11_PARSE(app, argc, argv);

  const auto ep = gw::parse_listen(listen);
  if (!ep) {
    spdlog::error("--listen {}: expected IP:port, such as 127.0.0.1:8080 or [::1]:8080",
                  listen);
    return 2;
  }
  if (!gw::is_loopback(ep->address())) {
    spdlog::error("refusing non-loopback listen address without TLS (arrives in v0.3)");
    return 2;
  }
  o.listen = *ep;

  try {
    return run(o);
  } catch (const std::exception& e) {
    spdlog::error("{}", e.what());
    return 1;
  }
}
