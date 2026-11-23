#include <string>

#include "skyrelay/vehicle/adapter.hpp"

namespace skyrelay::vehicle {


// PX4 packs its mode into custom_mode: main mode in byte 2, sub mode in byte 3.
ModeInfo Px4Adapter::decode_mode(uint8_t base_mode,
                                 uint32_t custom_mode) const {
  if (!(base_mode & MAV_MODE_FLAG_CUSTOM_MODE_ENABLED)) return {};

  const auto main = static_cast<uint8_t>((custom_mode >> 16) & 0xFF);
  const auto sub = static_cast<uint8_t>((custom_mode >> 24) & 0xFF);

  switch (main) {
    case 1: return {FlightMode::Manual, "MANUAL"};
    case 2: return {FlightMode::AltitudeHold, "ALTCTL"};
    case 3: return {FlightMode::PositionHold, "POSCTL"};
    case 4:
      switch (sub) {
        case 2: return {FlightMode::Takeoff, "AUTO.TAKEOFF"};
        case 3: return {FlightMode::Hold, "AUTO.LOITER"};
        case 4: return {FlightMode::Mission, "AUTO.MISSION"};
        case 5: return {FlightMode::Return, "AUTO.RTL"};
        case 6: return {FlightMode::Land, "AUTO.LAND"};
        default: break;
      }
      break;
    case 5: return {FlightMode::Acro, "ACRO"};
    case 6: return {FlightMode::Offboard, "OFFBOARD"};
    case 7: return {FlightMode::Stabilized, "STABILIZED"};
    default: break;
  }
  return {FlightMode::Unknown,
          "PX4:" + std::to_string(main) + "." + std::to_string(sub)};
}

}  // namespace skyrelay::vehicle
