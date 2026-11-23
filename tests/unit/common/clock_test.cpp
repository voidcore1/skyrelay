#include <gtest/gtest.h>

#include <chrono>

#include "skyrelay/common/clock.hpp"
#include "skyrelay/common/version.hpp"

TEST(FakeClock, AdvanceMovesBothClocks) {
  skyrelay::FakeClock c;
  c.set_realtime_ns(1'000);
  auto m0 = c.mono();
  c.advance(std::chrono::milliseconds(5));
  EXPECT_EQ(c.mono() - m0, std::chrono::milliseconds(5));
  EXPECT_EQ(c.realtime_ns(), 1'000 + 5'000'000u);
}

TEST(Version, ContractVersionIsPreview) {
  EXPECT_EQ(skyrelay::kContractVersion, "1.0.0-preview.1");
}
