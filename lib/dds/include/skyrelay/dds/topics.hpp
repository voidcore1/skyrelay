#pragma once

#include <string_view>

namespace skyrelay::dds {

// Topic names are part of the contract: skyrelay/v1/<name>. A breaking change
// means v2 topics, never a rename here.
inline constexpr std::string_view kTopicVehicleInfo = "skyrelay/v1/vehicle_info";
inline constexpr std::string_view kTopicVehicleState = "skyrelay/v1/vehicle_state";
inline constexpr std::string_view kTopicPose = "skyrelay/v1/pose";
inline constexpr std::string_view kTopicHealth = "skyrelay/v1/health";
inline constexpr std::string_view kTopicLinkStatus = "skyrelay/v1/link_status";

}  // namespace skyrelay::dds
