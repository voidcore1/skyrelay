#pragma once

#include <string>
#include <string_view>

namespace skyrelay::dds {

// Topic names are part of the contract: skyrelay/v1/<name>. A breaking change
// means v2 topics, never a rename here.
inline constexpr std::string_view kTopicVehicleInfo = "skyrelay/v1/vehicle_info";
inline constexpr std::string_view kTopicVehicleState = "skyrelay/v1/vehicle_state";
inline constexpr std::string_view kTopicPose = "skyrelay/v1/pose";
inline constexpr std::string_view kTopicHealth = "skyrelay/v1/health";
inline constexpr std::string_view kTopicLinkStatus = "skyrelay/v1/link_status";
inline constexpr std::string_view kTopicMissionState = "skyrelay/v1/mission_state";
inline constexpr std::string_view kTopicCommandRequest = "skyrelay/v1/command_request";
inline constexpr std::string_view kTopicCommandStatus = "skyrelay/v1/command_status";

// command_request is written on <fleet>.cmd, or on <fleet>.cmd.kill for the one
// kind that needs it (EMERGENCY_KILL). Everything else, including
// command_status, stays on the data partition <fleet>.
inline std::string command_partition(std::string_view fleet) {
  return std::string(fleet) + ".cmd";
}
inline std::string kill_partition(std::string_view fleet) {
  return std::string(fleet) + ".cmd.kill";
}

}  // namespace skyrelay::dds
