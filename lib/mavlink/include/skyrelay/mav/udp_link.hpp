#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <boost/asio.hpp>

#include "skyrelay/mav/link_uri.hpp"

namespace skyrelay::mav {

// Datagram transport for one vehicle. All I/O is asynchronous on the
// io_context passed in, so the constructor and send() never block.
class UdpLink {
 public:
  using RxHandler = std::function<void(std::span<const uint8_t>)>;

  // The largest UDP payload over IPv6 without jumbograms (IPv4 allows 65507).
  static constexpr std::size_t kMaxDatagram = 65527;

  struct Stats {
    uint64_t rx_datagrams = 0;
    uint64_t tx_datagrams = 0;
    uint64_t tx_dropped_no_peer = 0;
  };

  // UdpListen binds host:port and replies to whoever sent last. UdpOut binds
  // an ephemeral port and sends to host:port. A LinkUri built by hand may use
  // port 0 for a listen link; local_port() then reports what was bound.
  // on_rx gets each datagram whole and must not throw: an exception leaves
  // through io_context::run() and the link stops receiving.
  UdpLink(boost::asio::io_context& io, const LinkUri& uri, RxHandler on_rx);
  ~UdpLink();
  UdpLink(const UdpLink&) = delete;
  UdpLink& operator=(const UdpLink&) = delete;

  void send(std::span<const uint8_t> bytes);
  const Stats& stats() const { return st_->stats; }
  uint16_t local_port() const { return st_->socket.local_endpoint().port(); }

 private:
  // Everything an in-flight handler touches lives here and is kept alive by
  // the handlers themselves, so destroying the link before the io_context
  // has drained is safe.
  struct State {
    explicit State(boost::asio::io_context& io) : socket(io) {}
    boost::asio::ip::udp::socket socket;
    std::optional<boost::asio::ip::udp::endpoint> peer;
    bool fixed_peer = false;
    bool alive = true;
    boost::asio::ip::udp::endpoint rx_from;
    // Room for the largest UDP payload, so a datagram that packs many MAVLink
    // frames is never cut short.
    std::array<uint8_t, kMaxDatagram> rx_buf{};
    RxHandler on_rx;
    Stats stats;
  };
  static void arm_receive(const std::shared_ptr<State>& st);

  std::shared_ptr<State> st_;
};

}  // namespace skyrelay::mav
