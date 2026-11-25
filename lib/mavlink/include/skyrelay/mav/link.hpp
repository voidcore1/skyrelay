#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

#include <boost/asio.hpp>

#include "skyrelay/mav/link_uri.hpp"

namespace skyrelay::mav {

struct LinkCounters {
  uint64_t rx_chunks = 0;
  uint64_t tx_chunks = 0;
  // Sends that were not written: no peer, not connected, or too much waiting.
  uint64_t tx_dropped = 0;
  // Failed reads, and failed attempts to open or connect.
  uint64_t rx_errors = 0;
  // Successful connections or opens after the first one.
  uint64_t reconnects = 0;
};

// How long a stream link waits before trying again: `first`, doubling after
// each failed attempt up to `max`, and back to `first` after a success.
struct ReconnectBackoff {
  std::chrono::milliseconds first{500};
  std::chrono::milliseconds max{5000};
};

// Receives a line about a stream link's connection trouble (a failed open or
// connect, a lost connection). Called on the io_context's thread, rate limited
// by the link, and never after the link is destroyed.
using LogHandler = std::function<void(const std::string&)>;

// One transport to one vehicle. All I/O is asynchronous on the io_context the
// link was opened on, so send() never blocks and everything runs on the thread
// that runs that context.
class Link {
 public:
  // One received chunk: a datagram, or whatever one stream read returned.
  // Returns true when the chunk held a frame from the locked vehicle; a UDP
  // listen link replies only to senders that got true. The handler must not
  // throw: an exception leaves through io_context::run().
  using RxHandler = std::function<bool(std::span<const uint8_t>)>;

  virtual ~Link() = default;
  virtual void send(std::span<const uint8_t> bytes) = 0;
  virtual LinkCounters counters() const = 0;
  // The bound local UDP port; 0 for links that have none.
  virtual uint16_t local_port() const { return 0; }
};

// The link a URI names. A UDP link binds here and throws
// boost::system::system_error if it cannot; TCP and serial links keep trying in
// the background, so a vehicle that is not there yet does not stop the bridge;
// on_log hears about those failures.
std::unique_ptr<Link> open_link(boost::asio::io_context& io, const LinkUri& uri,
                                Link::RxHandler on_rx,
                                LogHandler on_log = nullptr);

}  // namespace skyrelay::mav
