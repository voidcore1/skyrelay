#pragma once

#include <array>
#include <cstdint>
#include <functional>
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

  struct Stats {
    uint64_t rx_datagrams = 0;
    uint64_t tx_datagrams = 0;
    uint64_t tx_dropped_no_peer = 0;
  };

  // UdpListen binds host:port and replies to whoever sent last. UdpOut binds
  // an ephemeral port and sends to host:port. A LinkUri built by hand may use
  // port 0 for a listen link; local_port() then reports what was bound.
  UdpLink(boost::asio::io_context& io, const LinkUri& uri, RxHandler on_rx);
  ~UdpLink();
  UdpLink(const UdpLink&) = delete;
  UdpLink& operator=(const UdpLink&) = delete;

  void send(std::span<const uint8_t> bytes);
  const Stats& stats() const { return stats_; }
  uint16_t local_port() const { return socket_.local_endpoint().port(); }

 private:
  void arm_receive();

  boost::asio::ip::udp::socket socket_;
  std::optional<boost::asio::ip::udp::endpoint> peer_;
  bool fixed_peer_;
  boost::asio::ip::udp::endpoint rx_from_;
  std::array<uint8_t, 2048> rx_buf_{};
  RxHandler on_rx_;
  Stats stats_;
};

}  // namespace skyrelay::mav
