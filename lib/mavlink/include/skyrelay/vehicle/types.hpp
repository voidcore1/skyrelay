#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace skyrelay::vehicle {

// Autopilot-neutral vehicle state. "Not available" is spelled NaN for floats
// and std::optional for everything else, because the DDS mapping turns that
// directly into the per-field validity bits.
//
// The enumerator order of every enum below is part of the contract: the IDL
// enums mirror it value for value, with Unknown first. Only append.

inline constexpr float kNaNf = std::numeric_limits<float>::quiet_NaN();
inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

enum class Autopilot : uint8_t { Unknown, Px4, ArduPilot, Other };

enum class VehicleType : uint8_t {
  Unknown,
  Multirotor,
  FixedWing,
  Vtol,
  Other
};

enum class FlightMode : uint8_t {
  Unknown,
  Manual,
  Stabilized,
  Acro,
  AltitudeHold,
  PositionHold,
  Hold,
  Mission,
  Return,
  Land,
  Takeoff,
  Guided,
  Offboard
};

enum class LandedState : uint8_t { Unknown, OnGround, InAir, TakingOff, Landing };

enum class GnssFix : uint8_t {
  Unknown,
  NoFix,
  Fix2D,
  Fix3D,
  Dgps,
  RtkFloat,
  RtkFixed
};

// raw is the autopilot's own name for the mode, kept so a mode we do not map
// is still visible to the operator.
struct ModeInfo {
  FlightMode mode = FlightMode::Unknown;
  std::string raw;

  bool operator==(const ModeInfo&) const = default;
};

struct PoseData {
  double lat_deg = kNaN;
  double lon_deg = kNaN;
  double alt_msl_m = kNaN;
  double alt_rel_home_m = kNaN;
  float q_w = kNaNf;
  float q_x = kNaNf;
  float q_y = kNaNf;
  float q_z = kNaNf;
  float vel_n_mps = kNaNf;
  float vel_e_mps = kNaNf;
  float vel_d_mps = kNaNf;
  float heading_deg = kNaNf;
  uint32_t time_boot_ms = 0;
};

struct StateData {
  std::optional<bool> armed;
  std::optional<ModeInfo> mode;
  std::optional<LandedState> landed;
  std::optional<uint8_t> system_status;
};

struct HealthData {
  float battery_voltage_v = kNaNf;
  float battery_current_a = kNaNf;
  float battery_remaining_pct = kNaNf;
  float battery_time_remaining_s = kNaNf;
  float gnss_hdop = kNaNf;
  std::optional<GnssFix> gnss_fix;
  std::optional<uint8_t> gnss_satellites;
  std::optional<uint32_t> sensors_present;
  std::optional<uint32_t> sensors_enabled;
  std::optional<uint32_t> sensors_healthy;
  std::optional<bool> estimator_ok;
};

struct InfoData {
  Autopilot autopilot = Autopilot::Unknown;
  std::optional<std::string> firmware_version;
  VehicleType vehicle_type = VehicleType::Unknown;
  uint8_t mav_sysid = 0;
  uint8_t mav_compid = 0;
  std::optional<uint64_t> capabilities;
};

}  // namespace skyrelay::vehicle
