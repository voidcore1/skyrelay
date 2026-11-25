#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "skyrelay/mav/command_transaction.hpp"
#include "skyrelay/mav/packer.hpp"

using namespace skyrelay::mav;

TEST(Packer, ChannelsKeepIndependentSequences) {
  Packer a(2, 245, 190), b(3, 245, 190);
  EXPECT_EQ(a.gcs_heartbeat().seq, 0);
  EXPECT_EQ(a.gcs_heartbeat().seq, 1);
  EXPECT_EQ(b.gcs_heartbeat().seq, 0);
}

TEST(Packer, CommandLongCarriesConfirmation) {
  Packer p(8, 245, MAV_COMP_ID_MISSIONPLANNER);
  auto m = p.command_long(1, 1, 511, {33, 50000, 0, 0, 0, 0, 0}, 2);
  mavlink_command_long_t c;
  mavlink_msg_command_long_decode(&m, &c);
  EXPECT_EQ(c.command, 511);
  EXPECT_FLOAT_EQ(c.param1, 33.f);
  EXPECT_FLOAT_EQ(c.param2, 50000.f);
  EXPECT_EQ(c.confirmation, 2);
  EXPECT_EQ(c.target_system, 1);
  EXPECT_EQ(m.sysid, 245);
}

TEST(Packer, GcsHeartbeatFields) {
  Packer p(9, 245, MAV_COMP_ID_MISSIONPLANNER);
  auto m = p.gcs_heartbeat();
  mavlink_heartbeat_t h;
  mavlink_msg_heartbeat_decode(&m, &h);
  EXPECT_EQ(h.type, MAV_TYPE_GCS);
  EXPECT_EQ(h.autopilot, MAV_AUTOPILOT_INVALID);
  EXPECT_EQ(m.compid, MAV_COMP_ID_MISSIONPLANNER);
}

TEST(Packer, CommandIntCarriesDegE7) {
  Packer p(10, 245, MAV_COMP_ID_MISSIONPLANNER);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  CommandSpec s{192, {-1, 1, 0, nan, 0, 0, 503.0f}, 1, 1};
  s.encoding = CommandEncoding::Int;
  s.frame = 0;
  s.x = 473981000;
  s.y = 85461000;
  auto m = p.command(SendRequest{s, 2});
  ASSERT_EQ(m.msgid, MAVLINK_MSG_ID_COMMAND_INT);
  mavlink_command_int_t c;
  mavlink_msg_command_int_decode(&m, &c);
  EXPECT_EQ(c.command, 192);
  EXPECT_EQ(c.frame, 0);
  EXPECT_EQ(c.x, 473981000);
  EXPECT_EQ(c.y, 85461000);
  EXPECT_FLOAT_EQ(c.z, 503.0f);
  EXPECT_FLOAT_EQ(c.param1, -1.0f);
  EXPECT_FLOAT_EQ(c.param2, 1.0f);
  EXPECT_FLOAT_EQ(c.param3, 0.0f);
  EXPECT_TRUE(std::isnan(c.param4));
  EXPECT_EQ(c.current, 0);
  EXPECT_EQ(c.autocontinue, 0);
  EXPECT_EQ(c.target_system, 1);
  EXPECT_EQ(c.target_component, 1);
  EXPECT_EQ(m.sysid, 245);
}

TEST(Packer, CommandLongFromSendRequest) {
  Packer p(11, 245, MAV_COMP_ID_MISSIONPLANNER);
  auto m = p.command(SendRequest{CommandSpec{400, {1, 0, 0, 0, 0, 0, 0}, 1, 1}, 2});
  ASSERT_EQ(m.msgid, MAVLINK_MSG_ID_COMMAND_LONG);
  mavlink_command_long_t c;
  mavlink_msg_command_long_decode(&m, &c);
  EXPECT_EQ(c.command, 400);
  EXPECT_FLOAT_EQ(c.param1, 1.0f);
  EXPECT_EQ(c.confirmation, 2);
  EXPECT_EQ(c.target_system, 1);
  EXPECT_EQ(c.target_component, 1);
}
