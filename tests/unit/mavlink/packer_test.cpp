#include <gtest/gtest.h>

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
