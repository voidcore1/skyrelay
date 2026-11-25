#include "skyrelay/dds/mapping.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

#include "skyrelay/common/version.hpp"
#include "skyrelay/dds/gen/v1_valid_bits.hpp"

namespace skyrelay::dds {
namespace {

namespace vd = vehicle;

// The IDL enums mirror the C++ ones value for value, but the conversion is
// spelled out so a reorder on either side fails here and not on the wire.
template <class E>
constexpr auto count_of(E last) {
  return static_cast<std::underlying_type_t<E>>(last) + 1;
}
static_assert(count_of(vd::Autopilot::Other) == 4);
static_assert(count_of(vd::VehicleType::Other) == 5);
static_assert(count_of(vd::FlightMode::Offboard) == 13);
static_assert(count_of(vd::LandedState::Landing) == 5);
static_assert(count_of(vd::GnssFix::RtkFixed) == 7);
static_assert(count_of(mav::LinkState::Degraded) == 4);

v1::AutopilotKind conv(vd::Autopilot a) {
  switch (a) {
    case vd::Autopilot::Unknown: return v1::AutopilotKind::AUTOPILOT_UNKNOWN;
    case vd::Autopilot::Px4: return v1::AutopilotKind::AUTOPILOT_PX4;
    case vd::Autopilot::ArduPilot: return v1::AutopilotKind::AUTOPILOT_ARDUPILOT;
    case vd::Autopilot::Other: return v1::AutopilotKind::AUTOPILOT_OTHER;
  }
  return v1::AutopilotKind::AUTOPILOT_UNKNOWN;
}

v1::VehicleType conv(vd::VehicleType t) {
  switch (t) {
    case vd::VehicleType::Unknown: return v1::VehicleType::VEHICLE_TYPE_UNKNOWN;
    case vd::VehicleType::Multirotor: return v1::VehicleType::VEHICLE_TYPE_MULTIROTOR;
    case vd::VehicleType::FixedWing: return v1::VehicleType::VEHICLE_TYPE_FIXED_WING;
    case vd::VehicleType::Vtol: return v1::VehicleType::VEHICLE_TYPE_VTOL;
    case vd::VehicleType::Other: return v1::VehicleType::VEHICLE_TYPE_OTHER;
  }
  return v1::VehicleType::VEHICLE_TYPE_UNKNOWN;
}

v1::FlightMode conv(vd::FlightMode m) {
  switch (m) {
    case vd::FlightMode::Unknown: return v1::FlightMode::FLIGHT_MODE_UNKNOWN;
    case vd::FlightMode::Manual: return v1::FlightMode::FLIGHT_MODE_MANUAL;
    case vd::FlightMode::Stabilized: return v1::FlightMode::FLIGHT_MODE_STABILIZED;
    case vd::FlightMode::Acro: return v1::FlightMode::FLIGHT_MODE_ACRO;
    case vd::FlightMode::AltitudeHold: return v1::FlightMode::FLIGHT_MODE_ALTITUDE_HOLD;
    case vd::FlightMode::PositionHold: return v1::FlightMode::FLIGHT_MODE_POSITION_HOLD;
    case vd::FlightMode::Hold: return v1::FlightMode::FLIGHT_MODE_HOLD;
    case vd::FlightMode::Mission: return v1::FlightMode::FLIGHT_MODE_MISSION;
    case vd::FlightMode::Return: return v1::FlightMode::FLIGHT_MODE_RETURN;
    case vd::FlightMode::Land: return v1::FlightMode::FLIGHT_MODE_LAND;
    case vd::FlightMode::Takeoff: return v1::FlightMode::FLIGHT_MODE_TAKEOFF;
    case vd::FlightMode::Guided: return v1::FlightMode::FLIGHT_MODE_GUIDED;
    case vd::FlightMode::Offboard: return v1::FlightMode::FLIGHT_MODE_OFFBOARD;
  }
  return v1::FlightMode::FLIGHT_MODE_UNKNOWN;
}

v1::LandedState conv(vd::LandedState s) {
  switch (s) {
    case vd::LandedState::Unknown: return v1::LandedState::LANDED_STATE_UNKNOWN;
    case vd::LandedState::OnGround: return v1::LandedState::LANDED_STATE_ON_GROUND;
    case vd::LandedState::InAir: return v1::LandedState::LANDED_STATE_IN_AIR;
    case vd::LandedState::TakingOff: return v1::LandedState::LANDED_STATE_TAKING_OFF;
    case vd::LandedState::Landing: return v1::LandedState::LANDED_STATE_LANDING;
  }
  return v1::LandedState::LANDED_STATE_UNKNOWN;
}

v1::GnssFix conv(vd::GnssFix f) {
  switch (f) {
    case vd::GnssFix::Unknown: return v1::GnssFix::GNSS_FIX_UNKNOWN;
    case vd::GnssFix::NoFix: return v1::GnssFix::GNSS_FIX_NO_FIX;
    case vd::GnssFix::Fix2D: return v1::GnssFix::GNSS_FIX_2D;
    case vd::GnssFix::Fix3D: return v1::GnssFix::GNSS_FIX_3D;
    case vd::GnssFix::Dgps: return v1::GnssFix::GNSS_FIX_DGPS;
    case vd::GnssFix::RtkFloat: return v1::GnssFix::GNSS_FIX_RTK_FLOAT;
    case vd::GnssFix::RtkFixed: return v1::GnssFix::GNSS_FIX_RTK_FIXED;
  }
  return v1::GnssFix::GNSS_FIX_UNKNOWN;
}

v1::LinkState conv(mav::LinkState s) {
  switch (s) {
    case mav::LinkState::Lost: return v1::LinkState::LINK_LOST;
    case mav::LinkState::Recovering: return v1::LinkState::LINK_RECOVERING;
    case mav::LinkState::Connected: return v1::LinkState::LINK_CONNECTED;
    case mav::LinkState::Degraded: return v1::LinkState::LINK_DEGRADED;
  }
  return v1::LinkState::LINK_LOST;
}

// Bounded IDL strings fail serialization when too long, so cut to the bound.
std::string bounded(const std::string& v, std::size_t max) {
  return v.substr(0, max);
}

v1::Header header(const vd::Stamp& st, uint64_t seq, uint64_t mask) {
  v1::Header h;
  h.seq(seq);
  h.t_source_us(st.t_source_us);
  h.t_bridge_ns(st.t_bridge_ns);
  h.valid_mask(mask);
  return h;
}

// Accumulates the mask while a field is copied, so the rule lives in one
// place: floats by isfinite, optionals by has_value.
struct Mask {
  uint64_t bits = 0;
  void set(uint64_t bit) { bits |= bit; }
  // The value to write: v if it is finite, with its bit set; otherwise NaN,
  // with the bit cleared. An infinity is no more a measurement than a NaN.
  template <class F>
  F flt(uint64_t bit, F v) {
    if (!std::isfinite(v)) return std::numeric_limits<F>::quiet_NaN();
    bits |= bit;
    return v;
  }
  template <class T>
  bool opt(uint64_t bit, const std::optional<T>& v) {
    if (!v) return false;
    bits |= bit;
    return true;
  }
};

}  // namespace

v1::VehicleInfo to_idl(uint32_t vid, const vd::InfoData& d, vd::Stamp st,
                       uint64_t seq) {
  using bit = v1::valid::VehicleInfo;
  v1::VehicleInfo s;
  Mask m;
  s.vehicle_id(vid);
  s.autopilot(conv(d.autopilot));
  s.vehicle_type(conv(d.vehicle_type));
  s.mav_sysid(d.mav_sysid);
  s.mav_compid(d.mav_compid);
  m.set(bit::autopilot | bit::vehicle_type | bit::mav_sysid | bit::mav_compid);
  if (m.opt(bit::firmware_version, d.firmware_version)) {
    s.firmware_version(bounded(*d.firmware_version, 32));
  }
  if (m.opt(bit::capabilities, d.capabilities)) s.capabilities(*d.capabilities);
  s.contract_version(bounded(std::string(kContractVersion), 16));
  m.set(bit::contract_version);
  s.header(header(st, seq, m.bits));
  return s;
}

v1::VehicleState to_idl(uint32_t vid, const vd::StateData& d, vd::Stamp st,
                        uint64_t seq) {
  using bit = v1::valid::VehicleState;
  v1::VehicleState s;
  Mask m;
  s.vehicle_id(vid);
  if (m.opt(bit::armed, d.armed)) s.armed(*d.armed);
  if (m.opt(bit::mode, d.mode)) {
    s.mode(conv(d.mode->mode));
    // The raw name is only "available" when the autopilot gave one.
    if (!d.mode->raw.empty()) {
      s.mode_raw(bounded(d.mode->raw, 32));
      m.set(bit::mode_raw);
    }
  }
  if (m.opt(bit::landed, d.landed)) s.landed(conv(*d.landed));
  if (m.opt(bit::system_status, d.system_status)) {
    s.system_status(*d.system_status);
  }
  s.header(header(st, seq, m.bits));
  return s;
}

v1::Pose to_idl(uint32_t vid, const vd::PoseData& d, vd::Stamp st,
                uint64_t seq) {
  using bit = v1::valid::Pose;
  v1::Pose s;
  Mask m;
  s.vehicle_id(vid);
  // An unavailable value is NaN in the sample as well as a cleared bit.
  s.lat_deg(m.flt(bit::lat_deg, d.lat_deg));
  s.lon_deg(m.flt(bit::lon_deg, d.lon_deg));
  s.alt_msl_m(m.flt(bit::alt_msl_m, d.alt_msl_m));
  s.alt_rel_home_m(m.flt(bit::alt_rel_home_m, d.alt_rel_home_m));
  s.q_w(m.flt(bit::q_w, d.q_w));
  s.q_x(m.flt(bit::q_x, d.q_x));
  s.q_y(m.flt(bit::q_y, d.q_y));
  s.q_z(m.flt(bit::q_z, d.q_z));
  s.vel_n_mps(m.flt(bit::vel_n_mps, d.vel_n_mps));
  s.vel_e_mps(m.flt(bit::vel_e_mps, d.vel_e_mps));
  s.vel_d_mps(m.flt(bit::vel_d_mps, d.vel_d_mps));
  s.heading_deg(m.flt(bit::heading_deg, d.heading_deg));
  s.header(header(st, seq, m.bits));
  return s;
}

v1::Health to_idl(uint32_t vid, const vd::HealthData& d, vd::Stamp st,
                  uint64_t seq) {
  using bit = v1::valid::Health;
  v1::Health s;
  Mask m;
  s.vehicle_id(vid);
  s.battery_voltage_v(m.flt(bit::battery_voltage_v, d.battery_voltage_v));
  s.battery_current_a(m.flt(bit::battery_current_a, d.battery_current_a));
  s.battery_remaining_pct(
      m.flt(bit::battery_remaining_pct, d.battery_remaining_pct));
  s.battery_time_remaining_s(
      m.flt(bit::battery_time_remaining_s, d.battery_time_remaining_s));
  s.gnss_hdop(m.flt(bit::gnss_hdop, d.gnss_hdop));
  s.gnss_fix(v1::GnssFix::GNSS_FIX_UNKNOWN);
  if (m.opt(bit::gnss_fix, d.gnss_fix)) s.gnss_fix(conv(*d.gnss_fix));
  if (m.opt(bit::gnss_satellites, d.gnss_satellites)) {
    s.gnss_satellites(*d.gnss_satellites);
  }
  if (m.opt(bit::sensors_present, d.sensors_present)) {
    s.sensors_present(*d.sensors_present);
  }
  if (m.opt(bit::sensors_enabled, d.sensors_enabled)) {
    s.sensors_enabled(*d.sensors_enabled);
  }
  if (m.opt(bit::sensors_healthy, d.sensors_healthy)) {
    s.sensors_healthy(*d.sensors_healthy);
  }
  if (m.opt(bit::estimator_ok, d.estimator_ok)) s.estimator_ok(*d.estimator_ok);
  s.header(header(st, seq, m.bits));
  return s;
}

v1::LinkStatus to_idl(uint32_t vid, const vd::LinkStatusData& d, vd::Stamp st,
                      uint64_t seq) {
  using bit = v1::valid::LinkStatus;
  v1::LinkStatus s;
  s.vehicle_id(vid);
  s.state(conv(d.state));
  s.state_since_ns(d.state_since_ns);
  s.rx_rate_hz(static_cast<float>(d.stats.rx_rate_hz));
  s.loss_pct(static_cast<float>(d.stats.loss_pct));
  // Clamped to the field: UINT32_MAX stands for "never received" (the
  // monitor's milliseconds::max()) and for anything 49.7 days or older.
  const int64_t age_ms = d.stats.last_rx_age.count();
  constexpr int64_t kNever = std::numeric_limits<uint32_t>::max();
  s.last_rx_age_ms(
      static_cast<uint32_t>(age_ms <= 0 ? 0 : std::min(age_ms, kNever)));
  // Signing is not implemented in this milestone: it is reported as off, and
  // that is a known value, not an unavailable one.
  s.signing(v1::SigningState::SIGNING_OFF);
  s.header(header(st, seq,
                  bit::state | bit::state_since_ns | bit::rx_rate_hz |
                      bit::loss_pct | bit::last_rx_age_ms | bit::signing));
  return s;
}

}  // namespace skyrelay::dds
