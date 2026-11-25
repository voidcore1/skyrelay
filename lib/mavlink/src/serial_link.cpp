#include "skyrelay/mav/serial_link.hpp"

#include <utility>

namespace skyrelay::mav {

namespace asio = boost::asio;

void SerialLink::Transport::open(
    std::function<void(const boost::system::error_code&)> done) {
  boost::system::error_code ec;
  port.open(device, ec);
  if (!ec) {
    // asio puts the line in raw mode on open; the rest is set here.
    port.set_option(asio::serial_port_base::baud_rate(baud), ec);
    if (!ec) port.set_option(asio::serial_port_base::character_size(8), ec);
    if (!ec) {
      port.set_option(
          asio::serial_port_base::parity(asio::serial_port_base::parity::none),
          ec);
    }
    if (!ec) {
      port.set_option(asio::serial_port_base::stop_bits(
                          asio::serial_port_base::stop_bits::one),
                      ec);
    }
    if (!ec) {
      port.set_option(asio::serial_port_base::flow_control(
                          asio::serial_port_base::flow_control::none),
                      ec);
    }
  }
  done(ec);
}

SerialLink::SerialLink(asio::io_context& io, const LinkUri& uri,
                       RxHandler on_rx, ReconnectBackoff backoff,
                       LogHandler on_log)
    : st_(std::make_shared<Core>(io, to_string(uri), std::move(on_rx), backoff,
                                 std::move(on_log))) {
  st_->transport.device = uri.device;
  st_->transport.baud = uri.baud;
  Core::start(st_);
}

SerialLink::~SerialLink() { Core::shutdown(st_); }

void SerialLink::send(std::span<const uint8_t> bytes) {
  Core::send(st_, bytes);
}

}  // namespace skyrelay::mav
