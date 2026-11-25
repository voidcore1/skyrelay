#include <gtest/gtest.h>

#include "skyrelay/dds/topics.hpp"

using namespace skyrelay::dds;

TEST(Topics, CommandPartitions) {
  EXPECT_EQ(command_partition("default"), "default.cmd");
  EXPECT_EQ(kill_partition("default"), "default.cmd.kill");
  EXPECT_EQ(command_partition("fleet-a"), "fleet-a.cmd");
}

TEST(Topics, NewTopicNamesAreVersioned) {
  EXPECT_EQ(kTopicMissionState, "skyrelay/v1/mission_state");
  EXPECT_EQ(kTopicCommandRequest, "skyrelay/v1/command_request");
  EXPECT_EQ(kTopicCommandStatus, "skyrelay/v1/command_status");
}
