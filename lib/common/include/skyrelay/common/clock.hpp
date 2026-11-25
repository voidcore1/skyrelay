#pragma once

#include <chrono>
#include <cstdint>

namespace skyrelay {

using MonoTime = std::chrono::steady_clock::time_point;

// Time source abstraction so that timing-dependent logic can be tested
// deterministically.
class Clock {
 public:
  virtual ~Clock() = default;

  // Monotonic time, for intervals and timeouts.
  virtual MonoTime mono() const = 0;
  // Wall-clock time in nanoseconds since the Unix epoch (CLOCK_REALTIME).
  virtual uint64_t realtime_ns() const = 0;
};

class SystemClock final : public Clock {
 public:
  MonoTime mono() const override;
  uint64_t realtime_ns() const override;
};

// Manually driven clock. Both time bases start at a fixed origin and only move
// when advance() is called.
class FakeClock final : public Clock {
 public:
  MonoTime mono() const override { return mono_; }
  uint64_t realtime_ns() const override { return realtime_ns_; }

  // Moves both the monotonic and the realtime clock forward.
  void advance(std::chrono::nanoseconds d);
  // Overrides the realtime clock without touching the monotonic one.
  void set_realtime_ns(uint64_t ns) { realtime_ns_ = ns; }

 private:
  MonoTime mono_{};
  uint64_t realtime_ns_{0};
};

}  // namespace skyrelay
