#include "skyrelay/mav/serial_link.hpp"

#include <algorithm>
#include <utility>

namespace skyrelay::mav {

namespace asio = boost::asio;

SerialLink::SerialLink(asio::io_context& io, const LinkUri& uri,
                       RxHandler on_rx, ReconnectBackoff backoff)
    : st_(std::make_shared<State>(io)) {
  st_->on_rx = std::move(on_rx);
  st_->device = uri.device;
  st_->baud = uri.baud;
  st_->backoff = backoff;
  st_->next_delay = backoff.first;
  try_open(st_);
}

SerialLink::~SerialLink() {
  st_->alive = false;
  st_->on_rx = nullptr;
  boost::system::error_code ec;
  st_->timer.cancel();
  st_->port.close(ec);
}

void SerialLink::try_open(const std::shared_ptr<State>& st) {
  boost::system::error_code ec;
  st->port.open(st->device, ec);
  if (!ec) {
    // asio puts the line in raw mode on open; the rest is set here.
    st->port.set_option(asio::serial_port_base::baud_rate(st->baud), ec);
    if (!ec) {
      st->port.set_option(asio::serial_port_base::character_size(8), ec);
    }
    if (!ec) {
      st->port.set_option(
          asio::serial_port_base::parity(asio::serial_port_base::parity::none),
          ec);
    }
    if (!ec) {
      st->port.set_option(asio::serial_port_base::stop_bits(
                              asio::serial_port_base::stop_bits::one),
                          ec);
    }
    if (!ec) {
      st->port.set_option(asio::serial_port_base::flow_control(
                              asio::serial_port_base::flow_control::none),
                          ec);
    }
  }
  if (ec) {
    ++st->counters.rx_errors;
    lost(st);
    return;
  }
  if (st->ever_opened) ++st->counters.reconnects;
  st->ever_opened = true;
  st->next_delay = st->backoff.first;
  arm_read(st);
}

void SerialLink::lost(const std::shared_ptr<State>& st) {
  ++st->generation;
  boost::system::error_code ec;
  st->port.close(ec);
  st->counters.tx_dropped += st->tx_queue.size();
  st->tx_queue.clear();
  st->tx_pending = 0;
  st->writing = false;

  const auto delay = st->next_delay;
  st->next_delay = std::min(st->next_delay * 2, st->backoff.max);
  st->timer.expires_after(delay);
  st->timer.async_wait([st](const boost::system::error_code& wait_ec) {
    if (!st->alive || wait_ec) return;
    try_open(st);
  });
}

void SerialLink::arm_read(const std::shared_ptr<State>& st) {
  const uint64_t gen = st->generation;
  st->port.async_read_some(
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
        if (auto cb = std::move(st->on_rx)) {
          cb(std::span<const uint8_t>(st->rx_buf.data(), n));
          if (st->alive) st->on_rx = std::move(cb);
        }
        if (!st->alive || gen != st->generation) return;
        arm_read(st);
      });
}

void SerialLink::send(std::span<const uint8_t> bytes) {
  auto& st = *st_;
  if (!st.port.is_open() || st.tx_pending + bytes.size() > kMaxPendingBytes) {
    ++st.counters.tx_dropped;
    return;
  }
  st.tx_queue.emplace_back(bytes.begin(), bytes.end());
  st.tx_pending += bytes.size();
  if (!st.writing) write_next(st_);
}

void SerialLink::write_next(const std::shared_ptr<State>& st) {
  if (st->tx_queue.empty()) {
    st->writing = false;
    return;
  }
  st->writing = true;
  const uint64_t gen = st->generation;
  asio::async_write(
      st->port, asio::buffer(st->tx_queue.front()),
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
