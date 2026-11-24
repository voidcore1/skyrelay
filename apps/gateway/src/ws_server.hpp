#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "skyrelay/common/clock.hpp"

namespace skyrelay::gateway {

// "127.0.0.1:8080" or "[::1]:8080". Only IP literals: the address that is
// checked is exactly the one that gets bound.
std::optional<boost::asio::ip::tcp::endpoint> parse_listen(std::string_view text);

// 127.0.0.0/8 or ::1.
bool is_loopback(const boost::asio::ip::address&);

struct ServerConfig {
  boost::asio::ip::tcp::endpoint listen;
  std::filesystem::path web_root;
  std::filesystem::path data_dir;
  // Topics whose latest value per vehicle a new WebSocket client gets.
  std::vector<std::string> latched_topics;
};

// HTTP and WebSocket server for the web station. GET/HEAD serve the station
// and the map data (see static_files.hpp), /healthz answers "ok", and /ws is
// the telemetry WebSocket described in docs/gateway-protocol.md.
//
// Everything runs on the thread that runs the io_context. The Fanout, the
// sessions and the acceptor are only touched there, so nothing is locked.
// post_sample(), stop() and counters() may be called from any thread.
class WsServer {
 public:
  struct Counters {
    std::atomic<uint64_t> ws_clients{0};           // connected WebSocket clients
    std::atomic<uint64_t> ws_writes_in_flight{0};  // at most one per client
    std::atomic<uint64_t> ignored_client_messages{0};
    std::atomic<uint64_t> samples{0};              // handed to the fan-out
  };

  // Binds and listens straight away, so a port in use fails here. Throws
  // std::invalid_argument for a non-loopback address: there is no TLS or
  // authentication in this preview.
  WsServer(boost::asio::io_context& io, ServerConfig config, const Clock& clock);
  // Only once the io_context is no longer running.
  ~WsServer();
  WsServer(const WsServer&) = delete;
  WsServer& operator=(const WsServer&) = delete;

  boost::asio::ip::tcp::endpoint local_endpoint() const;

  // Starts accepting and the fan-out timer, on the io_context thread.
  void start();

  // Hands one sample to the fan-out on the io_context thread. Called from the
  // DDS listener thread; never blocks.
  void post_sample(std::string_view topic, uint32_t vehicle_id, std::string json,
                   uint64_t t_bridge_ns);

  // Stops accepting, cancels the timer and closes every connection. Once the
  // handlers of the closed connections have run, the io_context runs out of
  // work and run() returns.
  void stop();

  const Counters& counters() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace skyrelay::gateway
