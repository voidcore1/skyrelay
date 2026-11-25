#include "skyrelay/common/clock.hpp"

#include <ctime>

namespace skyrelay {

MonoTime SystemClock::mono() const { return std::chrono::steady_clock::now(); }

uint64_t SystemClock::realtime_ns() const {
  timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000u +
         static_cast<uint64_t>(ts.tv_nsec);
}

void FakeClock::advance(std::chrono::nanoseconds d) {
  mono_ += d;
  realtime_ns_ += static_cast<uint64_t>(d.count());
}

}  // namespace skyrelay
