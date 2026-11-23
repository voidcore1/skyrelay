#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "skyrelay/mav/mavlink.hpp"

namespace skyrelay::mav {

struct FramerStats {
  uint64_t frames_ok = 0;
  uint64_t crc_errors = 0;
  uint64_t signature_errors = 0;
  // Bytes the parser skipped while hunting for a frame start, plus parse
  // errors it reported (oversized or malformed headers).
  uint64_t bytes_discarded = 0;
};

// Turns a byte stream into MAVLink messages. Not thread-safe; use one Framer
// per session, each on its own channel.
class Framer {
 public:
  explicit Framer(Channel ch);

  // Feeds bytes to the parser and returns every complete, valid message.
  // Frames that fail the CRC or signature check are counted and dropped.
  std::vector<mavlink_message_t> push(std::span<const uint8_t> bytes);

  const FramerStats& stats() const { return stats_; }

 private:
  Channel ch_;
  FramerStats stats_;
};

}  // namespace skyrelay::mav
