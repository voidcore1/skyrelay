#include "skyrelay/mav/udp_link.hpp"

#include <utility>

namespace skyrelay::mav {

namespace asio = boost::asio;
using asio::ip::udp;

UdpLink::UdpLink(asio::io_context& io, const LinkUri& uri, RxHandler on_rx)
    : st_(std::make_shared<State>(io)) {
  st_->fixed_peer = uri.scheme == LinkScheme::UdpOut;
  st_->on_rx = std::move(on_rx);
  // Literal addresses only; a hostname would need a resolver round trip.
  const auto address = asio::ip::make_address(uri.host);
  const auto protocol = address.is_v6() ? udp::v6() : udp::v4();
  st_->socket.open(protocol);
  if (uri.scheme == LinkScheme::UdpListen) {
    st_->socket.bind(udp::endpoint(address, uri.port));
  } else {
    st_->socket.bind(udp::endpoint(protocol, 0));
    st_->peer = udp::endpoint(address, uri.port);
  }
  arm_receive(st_);
}

UdpLink::~UdpLink() {
  // A completion may already be queued, so closing is not enough: mark the
  // state dead and drop the callback so late handlers do nothing.
  st_->alive = false;
  st_->on_rx = nullptr;
  boost::system::error_code ec;
  st_->socket.close(ec);
}

void UdpLink::arm_receive(const std::shared_ptr<State>& st) {
  st->socket.async_receive_from(
      asio::buffer(st->rx_buf), st->rx_from,
      [st](const boost::system::error_code& ec, std::size_t n) {
        if (!st->alive || ec == asio::error::operation_aborted) {
          return;
        }
        if (!ec) {
          ++st->stats.rx_datagrams;
          if (!st->fixed_peer) {
            st->peer = st->rx_from;
          }
          if (st->on_rx) {
            st->on_rx(std::span<const uint8_t>(st->rx_buf.data(), n));
          }
          if (!st->alive) {
            return;
          }
        }
        arm_receive(st);
      });
}

void UdpLink::send(std::span<const uint8_t> bytes) {
  if (!st_->peer) {
    ++st_->stats.tx_dropped_no_peer;
    return;
  }
  // The payload is copied because the send completes after this call returns.
  auto data = std::make_shared<std::vector<uint8_t>>(bytes.begin(), bytes.end());
  st_->socket.async_send_to(
      asio::buffer(*data), *st_->peer,
      [st = st_, data](const boost::system::error_code& ec, std::size_t) {
        if (!ec) {
          ++st->stats.tx_datagrams;
        }
      });
}

}  // namespace skyrelay::mav
