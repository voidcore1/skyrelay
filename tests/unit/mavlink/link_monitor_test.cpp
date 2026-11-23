#include <gtest/gtest.h>

#include <chrono>

#include "skyrelay/common/clock.hpp"
#include "skyrelay/mav/link_monitor.hpp"

using namespace skyrelay;
using namespace skyrelay::mav;
using namespace std::chrono_literals;

namespace {

class LinkMonitorTest : public ::testing::Test {
 protected:
  // Sends one message from compid 1 and advances time by 20 ms.
  void feed(int count) {
    for (int i = 0; i < count; ++i) {
      m.on_message(clock.mono(), 1, seq1++);
      clock.advance(20ms);
    }
  }
  MonoTime at(std::chrono::milliseconds d) const { return t0 + d; }

  // Brings the monitor to Connected; time ends 20 ms after the last message.
  void connect() {
    m.on_message(clock.mono(), 1, seq1++);
    clock.advance(20ms);
    m.on_setup_complete(clock.mono());
    feed(30);
    ASSERT_TRUE(m.tick(clock.mono()).has_value());
    ASSERT_EQ(m.state(), LinkState::Connected);
  }

  FakeClock clock;
  MonoTime t0 = clock.mono();
  LinkMonitor m;
  uint8_t seq1 = 0;
};

}  // namespace

TEST_F(LinkMonitorTest, StartsLost) {
  EXPECT_EQ(m.state(), LinkState::Lost);
  EXPECT_FALSE(m.tick(at(5s)).has_value());
}

TEST_F(LinkMonitorTest, FirstMessageLeavesLost) {
  auto tr = m.on_message(t0, 1, 0);
  ASSERT_TRUE(tr.has_value());
  EXPECT_EQ(tr->from, LinkState::Lost);
  EXPECT_EQ(tr->to, LinkState::Recovering);
  EXPECT_EQ(tr->at, t0);
  EXPECT_EQ(m.state(), LinkState::Recovering);
  EXPECT_EQ(m.state_since(), t0);
  EXPECT_FALSE(m.on_message(at(20ms), 1, 1).has_value());
}

TEST_F(LinkMonitorTest, ConnectsAfterSetupPlus500ms) {
  uint8_t s = 0;
  for (int i = 0; i < 6; ++i) m.on_message(at(i * 20ms), 1, s++);
  m.on_setup_complete(at(100ms));
  for (int i = 6; i <= 30; ++i) m.on_message(at(i * 20ms), 1, s++);
  EXPECT_FALSE(m.tick(at(599ms)).has_value());
  auto tr = m.tick(at(600ms));
  ASSERT_TRUE(tr.has_value());
  EXPECT_EQ(tr->from, LinkState::Recovering);
  EXPECT_EQ(tr->to, LinkState::Connected);
  EXPECT_EQ(m.state(), LinkState::Connected);
}

TEST_F(LinkMonitorTest, NoConnectWithoutSetup) {
  feed(100);
  EXPECT_FALSE(m.tick(clock.mono() - 20ms).has_value());
  EXPECT_EQ(m.state(), LinkState::Recovering);
}

TEST_F(LinkMonitorTest, RecoveringFallsBackToLostOnGap) {
  feed(5);
  const MonoTime last = clock.mono() - 20ms;
  EXPECT_FALSE(m.tick(last + 999ms).has_value());
  auto tr = m.tick(last + 1000ms);
  ASSERT_TRUE(tr.has_value());
  EXPECT_EQ(tr->to, LinkState::Lost);
}

TEST_F(LinkMonitorTest, DegradesAfter250msGap) {
  connect();
  const MonoTime last = clock.mono() - 20ms;
  EXPECT_FALSE(m.tick(last + 249ms).has_value());
  auto tr = m.tick(last + 250ms);
  ASSERT_TRUE(tr.has_value());
  EXPECT_EQ(tr->from, LinkState::Connected);
  EXPECT_EQ(tr->to, LinkState::Degraded);
  EXPECT_EQ(tr->reason, "gap");
}

TEST_F(LinkMonitorTest, LostAfter1000msGap) {
  connect();
  const MonoTime last = clock.mono() - 20ms;
  ASSERT_TRUE(m.tick(last + 250ms).has_value());
  EXPECT_FALSE(m.tick(last + 999ms).has_value());
  auto tr = m.tick(last + 1000ms);
  ASSERT_TRUE(tr.has_value());
  EXPECT_EQ(tr->from, LinkState::Degraded);
  EXPECT_EQ(tr->to, LinkState::Lost);
  EXPECT_EQ(m.state(), LinkState::Lost);
}

TEST_F(LinkMonitorTest, DegradesOnLossOver20Pct) {
  connect();
  std::optional<LinkTransition> tr;
  // Skip every 4th sequence number: 25 % loss.
  for (int i = 0; i < 50 && !tr; ++i) {
    if (i % 3 == 2) ++seq1;
    m.on_message(clock.mono(), 1, seq1++);
    clock.advance(20ms);
    tr = m.tick(clock.mono());
  }
  ASSERT_TRUE(tr.has_value());
  EXPECT_EQ(tr->to, LinkState::Degraded);
  EXPECT_EQ(tr->reason, "loss");
}

TEST_F(LinkMonitorTest, RecoversFromDegraded) {
  connect();
  for (int i = 0; i < 50; ++i) {
    if (i % 3 == 2) ++seq1;
    m.on_message(clock.mono(), 1, seq1++);
    clock.advance(20ms);
    m.tick(clock.mono());
  }
  ASSERT_EQ(m.state(), LinkState::Degraded);
  std::optional<LinkTransition> tr;
  for (int i = 0; i < 100 && !tr; ++i) {
    m.on_message(clock.mono(), 1, seq1++);
    clock.advance(20ms);
    tr = m.tick(clock.mono());
  }
  ASSERT_TRUE(tr.has_value());
  EXPECT_EQ(tr->from, LinkState::Degraded);
  EXPECT_EQ(tr->to, LinkState::Connected);
}

TEST_F(LinkMonitorTest, SeqWrapIsNotLoss) {
  uint8_t s = 250;
  for (int i = 0; i < 12; ++i) m.on_message(at(i * 20ms), 1, s++);
  EXPECT_DOUBLE_EQ(m.stats(at(240ms)).loss_pct, 0.0);
}

TEST_F(LinkMonitorTest, SeqDiscontinuityIsNotLoss) {
  m.on_message(at(0ms), 1, 100);
  uint8_t s = 3;
  for (int i = 1; i <= 50; ++i) m.on_message(at(i * 20ms), 1, s++);
  EXPECT_DOUBLE_EQ(m.stats(at(1000ms)).loss_pct, 0.0);
}

TEST_F(LinkMonitorTest, DuplicateIsIgnoredForLoss) {
  for (int i = 0; i < 10; ++i) {
    m.on_message(at(i * 40ms), 1, static_cast<uint8_t>(i));
    m.on_message(at(i * 40ms + 20ms), 1, static_cast<uint8_t>(i));
  }
  EXPECT_DOUBLE_EQ(m.stats(at(400ms)).loss_pct, 0.0);
}

TEST_F(LinkMonitorTest, PerComponentSequences) {
  uint8_t a = 0, b = 200;
  for (int i = 0; i < 50; ++i) {
    m.on_message(at(i * 20ms), 1, a++);
    m.on_message(at(i * 20ms), 100, b++);
  }
  EXPECT_DOUBLE_EQ(m.stats(at(1000ms)).loss_pct, 0.0);
}

TEST_F(LinkMonitorTest, RxRateOverWindow) {
  for (int i = 0; i < 50; ++i)
    m.on_message(at(i * 20ms), 1, static_cast<uint8_t>(i));
  EXPECT_NEAR(m.stats(at(1000ms)).rx_rate_hz, 50.0, 1.0);
  EXPECT_EQ(m.stats(at(1000ms)).last_rx_age, 20ms);
}
