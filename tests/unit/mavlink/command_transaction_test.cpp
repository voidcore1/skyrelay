#include <gtest/gtest.h>

#include "skyrelay/mav/command_transaction.hpp"

using namespace skyrelay;
using namespace skyrelay::mav;
using std::chrono::milliseconds;

namespace {

const MonoTime t0{};
MonoTime at(int ms) { return t0 + milliseconds{ms}; }

CommandSpec spec() { return CommandSpec{511, {33, 50000, 0, 0, 0, 0, 0}, 1, 1}; }

mavlink_command_ack_t ack(uint16_t command, uint8_t result, uint8_t target = 245,
                         uint8_t progress = 0) {
  mavlink_command_ack_t a{};
  a.command = command;
  a.result = result;
  a.target_system = target;
  a.progress = progress;
  return a;
}

}  // namespace

TEST(CommandTransaction, SendsThenRetriesAt350And700) {
  CommandTransaction tx(spec(), at(0));
  auto r0 = tx.poll(at(0));
  ASSERT_TRUE(r0);
  EXPECT_EQ(r0->confirmation, 0);
  EXPECT_EQ(r0->spec.command, 511);
  EXPECT_EQ(tx.state(), TxState::Sent);
  EXPECT_FALSE(tx.poll(at(349)));
  auto r1 = tx.poll(at(350));
  ASSERT_TRUE(r1);
  EXPECT_EQ(r1->confirmation, 1);
  EXPECT_FALSE(tx.poll(at(699)));
  auto r2 = tx.poll(at(700));
  ASSERT_TRUE(r2);
  EXPECT_EQ(r2->confirmation, 2);
  EXPECT_FALSE(tx.poll(at(999)));
}

TEST(CommandTransaction, TimesOutAtDeadline) {
  CommandTransaction tx(spec(), at(0));
  tx.poll(at(0));
  EXPECT_FALSE(tx.poll(at(1000)));
  EXPECT_EQ(tx.state(), TxState::TimedOut);
  EXPECT_TRUE(tx.terminal());
}

TEST(CommandTransaction, AcceptedStopsRetries) {
  CommandTransaction tx(spec(), at(0));
  tx.poll(at(0));
  tx.on_ack(ack(511, 0), 245, at(100));
  EXPECT_EQ(tx.state(), TxState::Accepted);
  EXPECT_TRUE(tx.terminal());
  EXPECT_FALSE(tx.poll(at(400)));
  ASSERT_TRUE(tx.mav_result());
  EXPECT_EQ(*tx.mav_result(), 0);
}

TEST(CommandTransaction, InProgressExtendsDeadline) {
  CommandTransaction tx(spec(), at(0));
  tx.poll(at(0));
  tx.on_ack(ack(511, 5), 245, at(200));
  EXPECT_FALSE(tx.poll(at(1500)));
  EXPECT_EQ(tx.state(), TxState::InProgress);
  EXPECT_FALSE(tx.terminal());
  EXPECT_FALSE(tx.poll(at(10000)));
  EXPECT_EQ(tx.state(), TxState::TimedOut);
}

TEST(CommandTransaction, MapsResults) {
  const TxState expected[] = {
      TxState::Accepted, TxState::Denied,     TxState::Denied,
      TxState::Denied,   TxState::Failed,     TxState::InProgress,
      TxState::Cancelled, TxState::Denied,    TxState::Denied,
      TxState::Denied};
  for (uint8_t r = 0; r <= 9; ++r) {
    CommandTransaction tx(spec(), at(0));
    tx.poll(at(0));
    tx.on_ack(ack(511, r), 245, at(10));
    EXPECT_EQ(tx.state(), expected[r]) << "result " << int(r);
    EXPECT_EQ(tx.terminal(), r != MAV_RESULT_IN_PROGRESS) << "result " << int(r);
  }
  // A result this build does not know is a failure.
  CommandTransaction tx(spec(), at(0));
  tx.poll(at(0));
  tx.on_ack(ack(511, 200), 245, at(10));
  EXPECT_EQ(tx.state(), TxState::Failed);
  EXPECT_EQ(tx.mav_result(), std::optional<uint8_t>{200});
}

TEST(CommandTransaction, ProgressFromInProgressAck) {
  CommandTransaction tx(spec(), at(0));
  EXPECT_EQ(tx.sends(), 0);
  tx.poll(at(0));
  EXPECT_EQ(tx.sends(), 1);
  EXPECT_FALSE(tx.progress());
  tx.poll(at(350));
  EXPECT_EQ(tx.sends(), 2);

  tx.on_ack(ack(511, MAV_RESULT_IN_PROGRESS, 245, 40), 245, at(400));
  EXPECT_EQ(tx.progress(), std::optional<uint8_t>{40});
  tx.on_ack(ack(511, MAV_RESULT_IN_PROGRESS, 245, 255), 245, at(500));
  EXPECT_FALSE(tx.progress());
  // 0-100 is a percentage; anything above it is not one.
  tx.on_ack(ack(511, MAV_RESULT_IN_PROGRESS, 245, 100), 245, at(510));
  EXPECT_EQ(tx.progress(), std::optional<uint8_t>{100});
  tx.on_ack(ack(511, MAV_RESULT_IN_PROGRESS, 245, 101), 245, at(520));
  EXPECT_FALSE(tx.progress());
  tx.on_ack(ack(511, MAV_RESULT_IN_PROGRESS, 245, 0), 245, at(530));
  EXPECT_EQ(tx.progress(), std::optional<uint8_t>{0});
  tx.on_ack(ack(511, MAV_RESULT_IN_PROGRESS, 245, 254), 245, at(540));
  EXPECT_FALSE(tx.progress());
  // No retries once the vehicle has said it is working on the command.
  EXPECT_FALSE(tx.poll(at(700)));
  EXPECT_EQ(tx.sends(), 2);

  tx.on_ack(ack(511, MAV_RESULT_IN_PROGRESS, 245, 90), 245, at(600));
  tx.on_ack(ack(511, MAV_RESULT_ACCEPTED), 245, at(700));
  EXPECT_EQ(tx.state(), TxState::Accepted);
  // The last IN_PROGRESS progress stays readable after the final ack.
  EXPECT_EQ(tx.progress(), std::optional<uint8_t>{90});
}

TEST(CommandTransaction, IgnoresForeignAcks) {
  CommandTransaction tx(spec(), at(0));
  tx.poll(at(0));
  tx.on_ack(ack(512, 0), 245, at(10));
  tx.on_ack(ack(511, 0, 200), 245, at(10));
  EXPECT_EQ(tx.state(), TxState::Sent);
  tx.on_ack(ack(511, 0, 0), 245, at(10));
  EXPECT_EQ(tx.state(), TxState::Accepted);
}
