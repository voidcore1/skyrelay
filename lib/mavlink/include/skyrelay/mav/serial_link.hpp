#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "skyrelay/mav/detail/stream_link_core.hpp"
#include "skyrelay/mav/link.hpp"

namespace skyrelay::mav {

// A serial device (a radio, or a flight controller's USB or UART port) opened
// raw, 8N1 without flow control, at the URI's baud rate. A read or write error
// closes the device and it is opened again after the backoff, for as long as
// the link lives; a failed open counts in rx_errors and waits the same way.
// A send while the device is closed is dropped and counted.
class SerialLink final : public Link {
 public:
  // More than this waiting to be written drops the send.
  static constexpr std::size_t kMaxPendingBytes = 64 * 1024;

  // Tries to open at once without blocking on retries: a device that is not
  // there yet is not an error, the link keeps trying. on_log hears about
  // failed opens.
  SerialLink(boost::asio::io_context& io, const LinkUri& uri, RxHandler on_rx,
             ReconnectBackoff backoff = {}, LogHandler on_log = nullptr);
  ~SerialLink() override;
  SerialLink(const SerialLink&) = delete;
  SerialLink& operator=(const SerialLink&) = delete;

  void send(std::span<const uint8_t> bytes) override;
  LinkCounters counters() const override { return st_->counters; }
  bool open() const { return st_->connected; }

 private:
  struct Transport {
    using Stream = boost::asio::serial_port;
    explicit Transport(boost::asio::io_context& io) : port(io) {}
    Stream& stream() { return port; }
    void open(std::function<void(const boost::system::error_code&)> done);
    void close() {
      boost::system::error_code ec;
      port.close(ec);
    }
    boost::asio::serial_port port;
    std::string device;
    uint32_t baud = 0;
  };
  using Core = detail::StreamLinkCore<Transport>;

  std::shared_ptr<Core> st_;
};

}  // namespace skyrelay::mav
