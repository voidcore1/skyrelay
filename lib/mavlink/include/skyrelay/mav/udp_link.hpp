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

#include "skyrelay/mav/link.hpp"

namespace skyrelay::mav {

// Datagram transport for one vehicle. All I/O is asynchronous on the
// io_context passed in, so the constructor and send() never block.
class UdpLink final : public Link {
 public:
  // The largest UDP payload over IPv6 without jumbograms (IPv4 allows 65507).
  static constexpr std::size_t kMaxDatagram = 65527;

  struct Stats {
    uint64_t rx_datagrams = 0;
    uint64_t tx_datagrams = 0;
    uint64_t tx_dropped_no_peer = 0;
    uint64_t rx_errors = 0;
  };

  // UdpListen binds host:port and replies to the sender of the last datagram
  // the handler accepted (returned true for). UdpOut binds
  // an ephemeral port and sends to host:port. A LinkUri built by hand may use
  // port 0 for a listen link; local_port() then reports what was bound.
  // on_rx gets each datagram whole and must not throw: an exception leaves
  // through io_context::run() and the link stops receiving. A listen link
  // without an on_rx never learns a peer.
  UdpLink(boost::asio::io_context& io, const LinkUri& uri, RxHandler on_rx);
  ~UdpLink() override;
  UdpLink(const UdpLink&) = delete;
  UdpLink& operator=(const UdpLink&) = delete;

  void send(std::span<const uint8_t> bytes) override;
  const Stats& stats() const { return st_->stats; }
  LinkCounters counters() const override {
    return LinkCounters{st_->stats.rx_datagrams, st_->stats.tx_datagrams,
                        st_->stats.tx_dropped_no_peer, st_->stats.rx_errors, 0};
  }
  uint16_t local_port() const override {
    return st_->socket.local_endpoint().port();
  }

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
