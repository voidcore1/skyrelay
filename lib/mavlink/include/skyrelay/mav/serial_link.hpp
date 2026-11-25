#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <vector>

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
  // there yet is not an error, the link keeps trying.
  SerialLink(boost::asio::io_context& io, const LinkUri& uri, RxHandler on_rx,
             ReconnectBackoff backoff = {});
  ~SerialLink() override;
  SerialLink(const SerialLink&) = delete;
  SerialLink& operator=(const SerialLink&) = delete;

  void send(std::span<const uint8_t> bytes) override;
  LinkCounters counters() const override { return st_->counters; }
  bool open() const { return st_->port.is_open(); }

 private:
  // As in TcpLink, everything an in-flight handler touches lives here.
  struct State {
    explicit State(boost::asio::io_context& io) : port(io), timer(io) {}
    boost::asio::serial_port port;
    boost::asio::steady_timer timer;
    std::string device;
    uint32_t baud = 0;
    ReconnectBackoff backoff;
    std::chrono::milliseconds next_delay{};
    bool alive = true;
    bool ever_opened = false;
    uint64_t generation = 0;
    std::array<uint8_t, 4096> rx_buf{};
    std::deque<std::vector<uint8_t>> tx_queue;
    std::size_t tx_pending = 0;
    bool writing = false;
    RxHandler on_rx;
    LinkCounters counters;
  };
  static void try_open(const std::shared_ptr<State>& st);
  static void arm_read(const std::shared_ptr<State>& st);
  static void write_next(const std::shared_ptr<State>& st);
  // Closes the device and schedules the next attempt to open it.
  static void lost(const std::shared_ptr<State>& st);

  std::shared_ptr<State> st_;
};

}  // namespace skyrelay::mav
