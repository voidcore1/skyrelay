#include "skyrelay/mav/udp_link.hpp"

#include <memory>
#include <utility>

namespace skyrelay::mav {

namespace asio = boost::asio;
using asio::ip::udp;

UdpLink::UdpLink(asio::io_context& io, const LinkUri& uri, RxHandler on_rx)
    : socket_(io),
      fixed_peer_(uri.scheme == LinkScheme::UdpOut),
      on_rx_(std::move(on_rx)) {
  // Literal addresses only; a hostname would need a resolver round trip.
  const auto address = asio::ip::make_address(uri.host);
  const auto protocol = address.is_v6() ? udp::v6() : udp::v4();
  socket_.open(protocol);
  if (uri.scheme == LinkScheme::UdpListen) {
    socket_.bind(udp::endpoint(address, uri.port));
  } else {
    socket_.bind(udp::endpoint(protocol, 0));
    peer_ = udp::endpoint(address, uri.port);
  }
  arm_receive();
}

UdpLink::~UdpLink() {
  boost::system::error_code ec;
  socket_.close(ec);
}

void UdpLink::arm_receive() {
  socket_.async_receive_from(
      asio::buffer(rx_buf_), rx_from_,
      [this](const boost::system::error_code& ec, std::size_t n) {
        if (ec == asio::error::operation_aborted) {
          return;
        }
        if (!ec) {
          ++stats_.rx_datagrams;
          if (!fixed_peer_) {
            peer_ = rx_from_;
          }
          if (on_rx_) {
            on_rx_(std::span<const uint8_t>(rx_buf_.data(), n));
          }
        }
        arm_receive();
      });
}

void UdpLink::send(std::span<const uint8_t> bytes) {
  if (!peer_) {
    ++stats_.tx_dropped_no_peer;
    return;
  }
  // The payload is copied because the send completes after this call returns.
  auto data = std::make_shared<std::vector<uint8_t>>(bytes.begin(), bytes.end());
  socket_.async_send_to(
      asio::buffer(*data), *peer_,
      [this, data](const boost::system::error_code& ec, std::size_t) {
        if (!ec) {
          ++stats_.tx_datagrams;
        }
      });
}

}  // namespace skyrelay::mav
