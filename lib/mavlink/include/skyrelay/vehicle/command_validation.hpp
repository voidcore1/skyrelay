#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "skyrelay/vehicle/command_types.hpp"

namespace skyrelay::vehicle {

// Every limit a request is checked against. docs/contract.md quotes these
// numbers in its validation table, and a unit test reads that table back, so
// a change here goes into the contract in the same commit.
inline constexpr float kMinAltitudeM = 1;           // alt_rel_home_m, inclusive
inline constexpr float kMaxAltitudeM = 500;         // inclusive
inline constexpr float kMaxGroundSpeedMps = 30;     // above 0, up to this
inline constexpr float kHeadingEndDeg = 360;        // headings are [0, 360)
inline constexpr double kMaxLatDeg = 90;            // [-90, 90]
inline constexpr double kMaxLonDeg = 180;           // [-180, 180]
inline constexpr uint32_t kMinDeadlineMs = 100;     // or 0 for the default
inline constexpr uint32_t kMaxDeadlineMs = 60000;   // inclusive
inline constexpr std::size_t kMaxWaypoints = 500;   // the IDL sequence bound
inline constexpr float kMaxHoldS = 3600;            // NaN, or [0, this]
inline constexpr float kMaxAcceptanceRadiusM = 1000;  // NaN, or (0, this]

// The configurable part of the limits. Positions and headings are not
// configurable: their ranges are what the numbers mean.
struct CommandLimits {
  float min_alt_m = kMinAltitudeM;
  float max_alt_m = kMaxAltitudeM;
  float max_speed_mps = kMaxGroundSpeedMps;
  uint32_t min_deadline_ms = kMinDeadlineMs;
  uint32_t max_deadline_ms = kMaxDeadlineMs;
  std::size_t max_waypoints = kMaxWaypoints;
  float max_hold_s = kMaxHoldS;
  float max_acceptance_radius_m = kMaxAcceptanceRadiusM;
};

// Checks a request before anything goes to the vehicle, in this order:
//  1. issuer_id and command_id are non-zero (REJECTED_INVALID);
//  2. the kind is one this build knows (REJECTED_UNSUPPORTED) and not
//     UNKNOWN (REJECTED_INVALID);
//  3. EMERGENCY_KILL arrives only on the kill partition
//     (REJECTED_UNAUTHORISED), nothing else does (REJECTED_INVALID), and a
//     kill carries confirm_token == command_id ^ vehicle_id (REJECTED_INVALID);
//  4. deadline_ms is 0 or within the limits;
//  5. only MISSION_UPLOAD carries waypoints;
//  6. the fields the kind reads are in range. Fields it does not read are not
//     looked at.
// The first failure wins. Range failures read
// "<field> out of range: <value> (allowed <min>-<max>)", with "waypoints[<i>]."
// in front for a waypoint field. A NaN where a value is needed is out of
// range.
std::optional<Rejection> validate_request(const CommandRequestData& req,
                                          CommandOrigin origin,
                                          const CommandLimits& limits = {});

}  // namespace skyrelay::vehicle
