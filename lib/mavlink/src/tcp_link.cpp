#include "skyrelay/mav/tcp_link.hpp"

#include <algorithm>
#include <utility>

namespace skyrelay::mav {

namespace asio = boost::asio;
using asio::ip::tcp;

TcpLink::TcpLink(asio::io_context& io, const LinkUri& uri, RxHandler on_rx,
                 ReconnectBackoff backoff)
    : st_(std::make_shared<State>(io)) {
  st_->on_rx = std::move(on_rx);
  st_->backoff = backoff;
  st_->next_delay = backoff.first;
  // Literal addresses only; a hostname would need a resolver round trip.
  st_->peer = tcp::endpoint(asio::ip::make_address(uri.host), uri.port);
  connect(st_);
}

TcpLink::~TcpLink() {
  // A completion may already be queued, so closing is not enough: mark the
  // state dead and drop the callback so late handlers do nothing.
  st_->alive = false;
  st_->on_rx = nullptr;
  boost::system::error_code ec;
  st_->timer.cancel();
  st_->socket.close(ec);
}

void TcpLink::connect(const std::shared_ptr<State>& st) {
  const uint64_t gen = st->generation;
  st->socket.async_connect(
      st->peer, [st, gen](const boost::system::error_code& ec) {
        if (!st->alive || gen != st->generation ||
            ec == asio::error::operation_aborted) {
          return;
        }
        if (ec) {
          ++st->counters.rx_errors;
          lost(st);
          return;
        }
        st->connected = true;
        if (st->ever_connected) ++st->counters.reconnects;
        st->ever_connected = true;
        st->next_delay = st->backoff.first;
        arm_read(st);
      });
}

void TcpLink::lost(const std::shared_ptr<State>& st) {
  ++st->generation;
  st->connected = false;
  boost::system::error_code ec;
  st->socket.close(ec);
  // What was waiting for the old connection is not replayed on the new one:
  // MAVLink traffic goes stale.
  st->counters.tx_dropped += st->tx_queue.size();
  st->tx_queue.clear();
  st->tx_pending = 0;
  st->writing = false;

  const auto delay = st->next_delay;
  st->next_delay = std::min(st->next_delay * 2, st->backoff.max);
  st->timer.expires_after(delay);
  st->timer.async_wait([st](const boost::system::error_code& wait_ec) {
    if (!st->alive || wait_ec) return;
    connect(st);
  });
}

void TcpLink::arm_read(const std::shared_ptr<State>& st) {
  const uint64_t gen = st->generation;
  st->socket.async_read_some(
      asio::buffer(st->rx_buf),
      [st, gen](const boost::system::error_code& ec, std::size_t n) {
        if (!st->alive || gen != st->generation ||
            ec == asio::error::operation_aborted) {
          return;
        }
        if (ec) {
          ++st->counters.rx_errors;
          lost(st);
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

void TcpLink::send(std::span<const uint8_t> bytes) {
  auto& st = *st_;
  if (!st.connected || st.tx_pending + bytes.size() > kMaxPendingBytes) {
    ++st.counters.tx_dropped;
    return;
  }
  st.tx_queue.emplace_back(bytes.begin(), bytes.end());
  st.tx_pending += bytes.size();
  if (!st.writing) write_next(st_);
}

void TcpLink::write_next(const std::shared_ptr<State>& st) {
  if (st->tx_queue.empty()) {
    st->writing = false;
    return;
  }
  st->writing = true;
  const uint64_t gen = st->generation;
  // The front chunk stays queued until its write completes, so the buffer
  // outlives the operation.
  asio::async_write(
      st->socket, asio::buffer(st->tx_queue.front()),
      [st, gen](const boost::system::error_code& ec, std::size_t n) {
        if (!st->alive || gen != st->generation ||
            ec == asio::error::operation_aborted) {
          return;
        }
        if (ec) {
          lost(st);
          return;
        }
        ++st->counters.tx_chunks;
        st->tx_pending -= n;
        st->tx_queue.pop_front();
        write_next(st);
      });
}

}  // namespace skyrelay::mav
