#include <gtest/gtest.h>

#include <chrono>
#include <limits>
#include <stdexcept>

#include "skyrelay/common/rate_limiter.hpp"

using namespace skyrelay;
using namespace std::chrono_literals;

namespace {
const MonoTime t0{};
}  // namespace

TEST(RateLimiter, FirstEventPasses) {
  RateLimiter r(20.0);
  EXPECT_TRUE(r.allow(t0));
  EXPECT_FALSE(r.allow(t0 + 10ms));
}

TEST(RateLimiter, CapsAFasterSource) {
  RateLimiter r(20.0);
  int passed = 0;
  for (int ms = 0; ms < 10'000; ms += 10) {
    if (r.allow(t0 + std::chrono::milliseconds{ms})) ++passed;
  }
  EXPECT_GE(passed, 199);
  EXPECT_LE(passed, 201);
}

// A source at exactly the cap arrives a few ms early or late; none of it may
// be dropped.
TEST(RateLimiter, PassesAJitteredSourceAtTheCap) {
  RateLimiter r(20.0);
  int passed = 0;
  for (int i = 0; i < 200; ++i) {
    const int jitter = (i % 2 == 0) ? -6 : 6;
    if (r.allow(t0 + 1s + std::chrono::milliseconds{i * 50 + jitter})) ++passed;
  }
  EXPECT_EQ(passed, 200);
}

TEST(RateLimiter, StartsAfreshAfterAPause) {
  RateLimiter r(1.0);
  EXPECT_TRUE(r.allow(t0));
  EXPECT_TRUE(r.allow(t0 + 10s));
  EXPECT_FALSE(r.allow(t0 + 10s + 500ms));
  EXPECT_TRUE(r.allow(t0 + 11s));
}

TEST(RateLimiter, RejectsRatesThatAreNotPositive) {
  EXPECT_THROW(RateLimiter(0.0), std::invalid_argument);
  EXPECT_THROW(RateLimiter(-1.0), std::invalid_argument);
  EXPECT_THROW(RateLimiter(std::numeric_limits<double>::quiet_NaN()),
               std::invalid_argument);
  EXPECT_THROW(RateLimiter(std::numeric_limits<double>::infinity()),
               std::invalid_argument);
}
