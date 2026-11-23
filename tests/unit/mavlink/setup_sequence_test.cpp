#include <gtest/gtest.h>

#include "skyrelay/mav/setup_sequence.hpp"

using namespace skyrelay;
using namespace skyrelay::mav;
using std::chrono::milliseconds;

namespace {

const MonoTime t0{};
MonoTime at(int ms) { return t0 + milliseconds{ms}; }

mavlink_command_ack_t ack(uint16_t command, uint8_t result) {
  mavlink_command_ack_t a{};
  a.command = command;
  a.result = result;
  a.target_system = 245;
  return a;
}

}  // namespace

TEST(SetupSequence, RequestsVersionThenIntervalsInOrder) {
  SetupSequence s({{33, 20.0}, {31, 50.0}}, 1, 1, at(0));

  auto a = s.poll(at(0));
  ASSERT_TRUE(a);
  EXPECT_EQ(a->spec.command, 512);
  EXPECT_FLOAT_EQ(a->spec.params[0], 148.f);
  // Nothing else goes out while the first command is in flight.
  EXPECT_FALSE(s.poll(at(10)));
  s.on_ack(ack(512, 0), 245, at(20));

  auto b = s.poll(at(20));
  ASSERT_TRUE(b);
  EXPECT_EQ(b->spec.command, 511);
  EXPECT_FLOAT_EQ(b->spec.params[0], 33.f);
  EXPECT_FLOAT_EQ(b->spec.params[1], 50000.f);
  EXPECT_EQ(b->confirmation, 0);
  s.on_ack(ack(511, 0), 245, at(30));

  auto c = s.poll(at(30));
  ASSERT_TRUE(c);
  EXPECT_EQ(c->spec.command, 511);
  EXPECT_FLOAT_EQ(c->spec.params[0], 31.f);
  EXPECT_FLOAT_EQ(c->spec.params[1], 20000.f);
  EXPECT_FALSE(s.done());
  s.on_ack(ack(511, 0), 245, at(40));

  EXPECT_TRUE(s.done());
  EXPECT_FALSE(s.poll(at(50)));
  EXPECT_TRUE(s.failed_msg_ids().empty());
}

TEST(SetupSequence, ContinuesAfterFailure) {
  SetupSequence s({{33, 20.0}, {31, 50.0}}, 1, 1, at(0));
  s.poll(at(0));
  s.on_ack(ack(512, 0), 245, at(10));
  s.poll(at(10));
  s.on_ack(ack(511, 2), 245, at(20));  // DENIED for 33

  auto c = s.poll(at(20));
  ASSERT_TRUE(c);
  EXPECT_FLOAT_EQ(c->spec.params[0], 31.f);
  s.on_ack(ack(511, 0), 245, at(30));

  EXPECT_TRUE(s.done());
  EXPECT_EQ(s.failed_msg_ids(), (std::vector<uint32_t>{33}));
}

TEST(SetupSequence, TimeoutCountsAsFailure) {
  SetupSequence s({{33, 20.0}}, 1, 1, at(0));
  s.poll(at(0));
  s.on_ack(ack(512, 0), 245, at(10));
  s.poll(at(10));
  auto r = s.poll(at(1010));
  EXPECT_FALSE(r);
  EXPECT_TRUE(s.done());
  EXPECT_EQ(s.failed_msg_ids(), (std::vector<uint32_t>{33}));
}
