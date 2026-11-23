#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

#include "skyrelay/common/clock.hpp"

namespace skyrelay {

// Caps an event stream at max_hz. Allowed events sit on a grid of slots one
// period apart, and an event up to a quarter period early takes the next slot.
// That way a source running at exactly the cap passes in full despite arrival
// jitter, while a faster source is cut down to the cap: over any window the
// count stays within two of max_hz times its length. After a pause longer
// than a period the grid restarts at the next event. Not thread-safe.
class RateLimiter {
 public:
  explicit RateLimiter(double max_hz) {
    // Below one event in about 30 years the period no longer fits in ns.
    if (!std::isfinite(max_hz) || max_hz < 1e-9) {
      throw std::invalid_argument("rate limit must be a positive number");
    }
    period_ = std::chrono::nanoseconds{
        std::max(1LL, static_cast<long long>(std::llround(1e9 / max_hz)))};
    slack_ = period_ / 4;
  }

  bool allow(MonoTime now) {
    if (started_ && now < next_ - slack_) return false;
    next_ = (started_ && now < next_ + period_) ? next_ + period_
                                                : now + period_;
    started_ = true;
    return true;
  }

 private:
  std::chrono::nanoseconds period_{};
  std::chrono::nanoseconds slack_{};
  MonoTime next_{};
  bool started_ = false;
};

}  // namespace skyrelay
