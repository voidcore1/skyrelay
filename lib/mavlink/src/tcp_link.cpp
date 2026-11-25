#include "skyrelay/mav/tcp_link.hpp"

#include <utility>

namespace skyrelay::mav {

namespace asio = boost::asio;
using asio::ip::tcp;

void TcpLink::Transport::open(
    std::function<void(const boost::system::error_code&)> done) {
  socket.async_connect(
      peer, [this, done = std::move(done)](const boost::system::error_code& ec) {
        if (!ec) {
          // Commands are small frames and their ack latency matters, so they
          // must not wait for Nagle's timer. A failure here only costs latency.
          boost::system::error_code ignored;
          socket.set_option(tcp::no_delay(true), ignored);
        }
        done(ec);
      });
}

TcpLink::TcpLink(asio::io_context& io, const LinkUri& uri, RxHandler on_rx,
                 ReconnectBackoff backoff, LogHandler on_log)
    : st_(std::make_shared<Core>(io, to_string(uri), std::move(on_rx), backoff,
                                 std::move(on_log))) {
  // Literal addresses only; a hostname would need a resolver round trip.
  st_->transport.peer =
      tcp::endpoint(asio::ip::make_address(uri.host), uri.port);
  Core::start(st_);
}

TcpLink::~TcpLink() { Core::shutdown(st_); }

void TcpLink::send(std::span<const uint8_t> bytes) {
  Core::send(st_, bytes);
}

}  // namespace skyrelay::mav
