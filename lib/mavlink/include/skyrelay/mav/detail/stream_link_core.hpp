#pragma once

// The part TcpLink and SerialLink have in common: reading, a bounded write
// queue, and reopening after a failure with a backoff. It is the lifetime-
// critical code of both links, so it lives in one place.
//
// A Transport supplies the device-specific half:
//   using Stream = ...;                      // async read/write stream
//   explicit Transport(boost::asio::io_context&);
//   Stream& stream();
//   void open(std::function<void(const boost::system::error_code&)> done);
//   void close();                            // must not throw
// open() may call done() before it returns.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

#include "skyrelay/mav/link.hpp"

namespace skyrelay::mav::detail {

template <class Transport>
struct StreamLinkCore {
  static constexpr std::size_t kMaxPendingBytes = 64 * 1024;
  using Ptr = std::shared_ptr<StreamLinkCore>;

  StreamLinkCore(boost::asio::io_context& io, std::string name,
                 Link::RxHandler rx, ReconnectBackoff b, LogHandler lg)
      : transport(io),
        timer(io),
        label(std::move(name)),
        backoff(b),
        next_delay(b.first),
        on_rx(std::move(rx)),
        log(std::move(lg)) {}

  // Everything an in-flight handler touches lives here and is kept alive by
  // the handlers themselves, so destroying the link before the io_context
  // has drained is safe.
  Transport transport;
  boost::asio::steady_timer timer;
  std::string label;
  ReconnectBackoff backoff;
  std::chrono::milliseconds next_delay;
  bool alive = true;
  bool connected = false;
  bool ever_connected = false;
  // Bumped whenever a connection ends, so a handler that belongs to an
  // earlier one does nothing.
  uint64_t generation = 0;
  uint64_t failure_streak = 0;
  std::array<uint8_t, 4096> rx_buf{};
  std::deque<std::vector<uint8_t>> tx_queue;
  std::size_t tx_pending = 0;
  bool writing = false;
  Link::RxHandler on_rx;
  LogHandler log;
  LinkCounters counters;

  // A completion may already be queued, so closing is not enough: mark the
  // state dead and drop the callbacks so late handlers do nothing.
  static void shutdown(const Ptr& st) {
    st->alive = false;
    st->on_rx = nullptr;
    st->log = nullptr;
    st->timer.cancel();
    st->transport.close();
  }

  static void start(const Ptr& st) {
    const uint64_t gen = st->generation;
    st->transport.open([st, gen](const boost::system::error_code& ec) {
      if (!st->alive || gen != st->generation ||
          ec == boost::asio::error::operation_aborted) {
        return;
      }
      if (ec) {
        lost(st, "cannot open", ec, true);
        return;
      }
      st->connected = true;
      if (st->ever_connected) ++st->counters.reconnects;
      st->ever_connected = true;
      st->next_delay = st->backoff.first;
      st->failure_streak = 0;
      arm_read(st);
    });
  }

  static void send(const Ptr& st, std::span<const uint8_t> bytes) {
    if (!st->connected || st->tx_pending + bytes.size() > kMaxPendingBytes) {
      ++st->counters.tx_dropped;
      return;
    }
    st->tx_queue.emplace_back(bytes.begin(), bytes.end());
    st->tx_pending += bytes.size();
    if (!st->writing) write_next(st);
  }

 private:
  // Ends the current connection (or failed attempt), says why, and schedules
  // the next attempt.
  static void lost(const Ptr& st, const char* what,
                   const boost::system::error_code& ec, bool count_error) {
    ++st->generation;
    st->connected = false;
    st->transport.close();
    if (count_error) ++st->counters.rx_errors;
    // What was waiting for the old connection is not replayed on the new one:
    // MAVLink traffic goes stale.
    st->counters.tx_dropped += st->tx_queue.size();
    st->tx_queue.clear();
    st->tx_pending = 0;
    st->writing = false;

    const auto delay = st->next_delay;
    st->next_delay = std::min(st->next_delay * 2, st->backoff.max);
    // Logged at the first failure and at every step while the delay is still
    // growing; once it sits at its maximum, about once a minute.
    ++st->failure_streak;
    const bool ramping = delay < st->backoff.max;
    if (st->log &&
        (st->failure_streak == 1 || ramping || st->failure_streak % 12 == 0)) {
      st->log(st->label + ": " + what + ": " + ec.message() +
              ", trying again in " + std::to_string(delay.count()) + " ms");
    }
    st->timer.expires_after(delay);
    st->timer.async_wait([st](const boost::system::error_code& wait_ec) {
      if (!st->alive || wait_ec) return;
      start(st);
    });
  }

  static void arm_read(const Ptr& st) {
    const uint64_t gen = st->generation;
    st->transport.stream().async_read_some(
        boost::asio::buffer(st->rx_buf),
        [st, gen](const boost::system::error_code& ec, std::size_t n) {
          if (!st->alive || gen != st->generation ||
              ec == boost::asio::error::operation_aborted) {
            return;
          }
          if (ec) {
            lost(st, "read failed", ec, true);
            return;
          }
          ++st->counters.rx_chunks;
          // Moved out while it runs, so a handler that destroys the link does
          // not destroy itself mid-call.
          if (auto cb = std::move(st->on_rx)) {
            cb(std::span<const uint8_t>(st->rx_buf.data(), n));
            if (st->alive) st->on_rx = std::move(cb);
          }
          if (!st->alive || gen != st->generation) return;
          arm_read(st);
        });
  }

  static void write_next(const Ptr& st) {
    if (st->tx_queue.empty()) {
      st->writing = false;
      return;
    }
    st->writing = true;
    const uint64_t gen = st->generation;
    // The front chunk stays queued until its write completes, so the buffer
    // outlives the operation.
    boost::asio::async_write(
        st->transport.stream(), boost::asio::buffer(st->tx_queue.front()),
        [st, gen](const boost::system::error_code& ec, std::size_t n) {
          if (!st->alive || gen != st->generation ||
              ec == boost::asio::error::operation_aborted) {
            return;
          }
          if (ec) {
            lost(st, "write failed", ec, false);
            return;
          }
          ++st->counters.tx_chunks;
          st->tx_pending -= n;
          st->tx_queue.pop_front();
          write_next(st);
        });
  }
};

}  // namespace skyrelay::mav::detail
