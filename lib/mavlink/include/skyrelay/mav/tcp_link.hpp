#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "skyrelay/mav/detail/stream_link_core.hpp"
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
  // boost::system::system_error. on_log hears about failed connects.
  TcpLink(boost::asio::io_context& io, const LinkUri& uri, RxHandler on_rx,
          ReconnectBackoff backoff = {}, LogHandler on_log = nullptr);
  ~TcpLink() override;
  TcpLink(const TcpLink&) = delete;
  TcpLink& operator=(const TcpLink&) = delete;

  void send(std::span<const uint8_t> bytes) override;
  LinkCounters counters() const override { return st_->counters; }
  bool connected() const { return st_->connected; }

 private:
  struct Transport {
    using Stream = boost::asio::ip::tcp::socket;
    explicit Transport(boost::asio::io_context& io) : socket(io) {}
    Stream& stream() { return socket; }
    void open(std::function<void(const boost::system::error_code&)> done);
    void close() {
      boost::system::error_code ec;
      socket.close(ec);
    }
    boost::asio::ip::tcp::socket socket;
    boost::asio::ip::tcp::endpoint peer;
  };
  using Core = detail::StreamLinkCore<Transport>;

  std::shared_ptr<Core> st_;
};

}  // namespace skyrelay::mav
