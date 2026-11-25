#include "skyrelay/vehicle/command_validation.hpp"

#include <cmath>
#include <format>
#include <string>
#include <string_view>

namespace skyrelay::vehicle {

namespace {

// Each test is written so that NaN fails it: every comparison with NaN is
// false.
bool closed(double v, double lo, double hi) { return v >= lo && v <= hi; }
bool above_up_to(double v, double lo, double hi) { return v > lo && v <= hi; }
bool from_below(double v, double lo, double hi) { return v >= lo && v < hi; }

template <class V, class L>
Rejection out_of_range(std::string_view prefix, std::string_view field, V value,
                       L lo, L hi) {
  return {CommandState::RejectedInvalid,
          std::format("{}{} out of range: {} (allowed {}-{})", prefix, field,
                      value, lo, hi)};
}

std::optional<Rejection> check_altitude(std::string_view prefix, float alt,
                                        const CommandLimits& l) {
  if (closed(alt, l.min_alt_m, l.max_alt_m)) return std::nullopt;
  return out_of_range(prefix, "alt_rel_home_m", alt, l.min_alt_m, l.max_alt_m);
}

std::optional<Rejection> check_position(std::string_view prefix, double lat,
                                        double lon) {
  if (!closed(lat, -kMaxLatDeg, kMaxLatDeg)) {
    return out_of_range(prefix, "lat_deg", lat, -kMaxLatDeg, kMaxLatDeg);
  }
  if (!closed(lon, -kMaxLonDeg, kMaxLonDeg)) {
    return out_of_range(prefix, "lon_deg", lon, -kMaxLonDeg, kMaxLonDeg);
  }
  return std::nullopt;
}

std::optional<Rejection> check_speed(float speed, const CommandLimits& l) {
  if (above_up_to(speed, 0, l.max_speed_mps)) return std::nullopt;
  return out_of_range("", "ground_speed_mps", speed, 0.0f, l.max_speed_mps);
}

std::optional<Rejection> check_heading(float heading) {
  if (from_below(heading, 0, kHeadingEndDeg)) return std::nullopt;
  return out_of_range("", "heading_deg", heading, 0.0f, kHeadingEndDeg);
}

std::optional<Rejection> check_goto(const CommandParams& p,
                                    const CommandLimits& l) {
  if (auto r = check_position("", p.lat_deg, p.lon_deg)) return r;
  if (auto r = check_altitude("", p.alt_rel_home_m, l)) return r;
  // NaN keeps the current speed and heading.
  if (!std::isnan(p.ground_speed_mps)) {
    if (auto r = check_speed(p.ground_speed_mps, l)) return r;
  }
  if (!std::isnan(p.heading_deg)) {
    if (auto r = check_heading(p.heading_deg)) return r;
  }
  return std::nullopt;
}

std::optional<Rejection> check_mission(const std::vector<Waypoint>& wps,
                                       const CommandLimits& l) {
  if (wps.empty() || wps.size() > l.max_waypoints) {
    return out_of_range("", "waypoints", wps.size(), std::size_t{1},
                        l.max_waypoints);
  }
  for (std::size_t i = 0; i < wps.size(); ++i) {
    const Waypoint& w = wps[i];
    const std::string prefix = std::format("waypoints[{}].", i);
    if (auto r = check_position(prefix, w.lat_deg, w.lon_deg)) return r;
    if (auto r = check_altitude(prefix, w.alt_rel_home_m, l)) return r;
    if (!std::isnan(w.hold_s) && !closed(w.hold_s, 0, l.max_hold_s)) {
      return out_of_range(prefix, "hold_s", w.hold_s, 0.0f, l.max_hold_s);
    }
    if (!std::isnan(w.acceptance_radius_m) &&
        !above_up_to(w.acceptance_radius_m, 0, l.max_acceptance_radius_m)) {
      return out_of_range(prefix, "acceptance_radius_m", w.acceptance_radius_m,
                          0.0f, l.max_acceptance_radius_m);
    }
  }
  return std::nullopt;
}

Rejection invalid(std::string detail) {
  return {CommandState::RejectedInvalid, std::move(detail)};
}

}  // namespace

std::optional<Rejection> validate_request(const CommandRequestData& req,
                                          CommandOrigin origin,
                                          const CommandLimits& limits) {
  if (req.key.issuer_id == 0 || req.key.command_id == 0) {
    return invalid("issuer_id and command_id must be non-zero");
  }
  if (req.unknown_kind) {
    return Rejection{CommandState::RejectedUnsupported,
                     std::format("unknown command kind {}", *req.unknown_kind)};
  }
  if (req.kind == CommandKind::Unknown) return invalid("kind is UNKNOWN");

  const bool is_kill = req.kind == CommandKind::EmergencyKill;
  if (is_kill && origin != CommandOrigin::Kill) {
    return Rejection{CommandState::RejectedUnauthorised,
                     "EMERGENCY_KILL is only accepted on the kill partition"};
  }
  if (!is_kill && origin == CommandOrigin::Kill) {
    return invalid("only EMERGENCY_KILL is accepted on the kill partition");
  }
  if (is_kill && req.confirm_token != (req.key.command_id ^ req.key.vehicle_id)) {
    return invalid("confirm_token does not match");
  }

  if (req.deadline_ms != 0 && (req.deadline_ms < limits.min_deadline_ms ||
                               req.deadline_ms > limits.max_deadline_ms)) {
    return out_of_range("", "deadline_ms", req.deadline_ms,
                        limits.min_deadline_ms, limits.max_deadline_ms);
  }
  if (req.kind != CommandKind::MissionUpload && !req.waypoints.empty()) {
    return invalid("waypoints are only used by MISSION_UPLOAD");
  }

  const CommandParams& p = req.params;
  switch (req.kind) {
    case CommandKind::Takeoff:
    case CommandKind::SetAltitude:
      return check_altitude("", p.alt_rel_home_m, limits);
    case CommandKind::Goto:
      return check_goto(p, limits);
    case CommandKind::SetSpeed:
      return check_speed(p.ground_speed_mps, limits);
    case CommandKind::SetHeading:
      return check_heading(p.heading_deg);
    case CommandKind::MissionUpload:
      return check_mission(req.waypoints, limits);
    default:
      return std::nullopt;
  }
}

}  // namespace skyrelay::vehicle
