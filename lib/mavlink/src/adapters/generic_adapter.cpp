#include <string>

#include "skyrelay/vehicle/adapter.hpp"

namespace skyrelay::vehicle {

std::vector<mav::IntervalRequest> AutopilotAdapter::telemetry_intervals()
    const {
  return {
      {MAVLINK_MSG_ID_GLOBAL_POSITION_INT, 20.0},
      {MAVLINK_MSG_ID_ATTITUDE_QUATERNION, 50.0},
      {MAVLINK_MSG_ID_SYS_STATUS, 2.0},
      {MAVLINK_MSG_ID_BATTERY_STATUS, 2.0},
      {MAVLINK_MSG_ID_GPS_RAW_INT, 2.0},
      {MAVLINK_MSG_ID_EXTENDED_SYS_STATE, 5.0},
      {MAVLINK_MSG_ID_SYSTEM_TIME, 1.0},
  };
}

ModeInfo GenericMavlinkAdapter::decode_mode(uint8_t base_mode,
                                            uint32_t custom_mode) const {
  if (!(base_mode & MAV_MODE_FLAG_CUSTOM_MODE_ENABLED)) return {};
  return {FlightMode::Unknown, "CUSTOM:" + std::to_string(custom_mode)};
}

std::unique_ptr<AutopilotAdapter> make_adapter(uint8_t mav_autopilot) {
  switch (mav_autopilot) {
    case MAV_AUTOPILOT_PX4:
      return std::make_unique<Px4Adapter>();
    case MAV_AUTOPILOT_ARDUPILOTMEGA:
      return std::make_unique<GenericMavlinkAdapter>(Autopilot::ArduPilot);
    default:
      return std::make_unique<GenericMavlinkAdapter>(Autopilot::Other);
  }
}

}  // namespace skyrelay::vehicle
