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

// Connects out to a vehicle's (or a proxy's) TCP port. It is a client only:
// a failed connect or a lost connection is retried after the backoff, for as
// long as the link lives. A send while not connected is dropped and counted.
class TcpLink final : public Link {
 public:
  // More than this waiting to be written (a peer that stopped reading) drops
  // the send.
  static constexpr std::size_t kMaxPendingBytes = 64 * 1024;

  // Starts connecting at once without blocking. uri.host must be an IP
  // literal (parse_link_uri guarantees it); anything else throws
  // boost::system::system_error.
  TcpLink(boost::asio::io_context& io, const LinkUri& uri, RxHandler on_rx,
          ReconnectBackoff backoff = {});
  ~TcpLink() override;
  TcpLink(const TcpLink&) = delete;
  TcpLink& operator=(const TcpLink&) = delete;

  void send(std::span<const uint8_t> bytes) override;
  LinkCounters counters() const override { return st_->counters; }
  bool connected() const { return st_->connected; }

 private:
  // Everything an in-flight handler touches lives here and is kept alive by
  // the handlers themselves, so destroying the link before the io_context
  // has drained is safe.
  struct State {
    explicit State(boost::asio::io_context& io) : socket(io), timer(io) {}
    boost::asio::ip::tcp::socket socket;
    boost::asio::steady_timer timer;
    boost::asio::ip::tcp::endpoint peer;
    ReconnectBackoff backoff;
    std::chrono::milliseconds next_delay{};
    bool alive = true;
    bool connected = false;
    bool ever_connected = false;
    // Bumped whenever a connection ends, so a handler that belongs to an
    // earlier connection does nothing.
    uint64_t generation = 0;
    std::array<uint8_t, 4096> rx_buf{};
    std::deque<std::vector<uint8_t>> tx_queue;
    std::size_t tx_pending = 0;
    bool writing = false;
    RxHandler on_rx;
    LinkCounters counters;
  };
  static void connect(const std::shared_ptr<State>& st);
  static void arm_read(const std::shared_ptr<State>& st);
  static void write_next(const std::shared_ptr<State>& st);
  // Ends the current connection (or failed attempt) and schedules the next.
  static void lost(const std::shared_ptr<State>& st);

  std::shared_ptr<State> st_;
};

}  // namespace skyrelay::mav
