#include "skyrelay/vehicle/vehicle_model.hpp"

#include <cmath>
#include <string>

namespace skyrelay::vehicle {

namespace {

// Stores value and reports whether it differs from what was there. A field
// that had no value yet counts as different.
template <typename T>
bool assign(T& field, const T& value) {
  const bool differs = field != value;
  field = value;
  return differs;
}

VehicleType vehicle_type_from(uint8_t mav_type) {
  switch (mav_type) {
    case MAV_TYPE_FIXED_WING:
      return VehicleType::FixedWing;
    case MAV_TYPE_COAXIAL:
    case MAV_TYPE_HELICOPTER:
    case MAV_TYPE_QUADROTOR:
    case MAV_TYPE_HEXAROTOR:
    case MAV_TYPE_OCTOROTOR:
    case MAV_TYPE_TRICOPTER:
    case MAV_TYPE_DODECAROTOR:
      return VehicleType::Multirotor;
    case MAV_TYPE_VTOL_TAILSITTER_DUOROTOR:
    case MAV_TYPE_VTOL_TAILSITTER_QUADROTOR:
    case MAV_TYPE_VTOL_TILTROTOR:
    case MAV_TYPE_VTOL_FIXEDROTOR:
    case MAV_TYPE_VTOL_TAILSITTER:
    case MAV_TYPE_VTOL_TILTWING:
    case MAV_TYPE_VTOL_RESERVED5:
      return VehicleType::Vtol;
    default:
      return VehicleType::Other;
  }
}

Autopilot autopilot_from(uint8_t mav_autopilot) {
  switch (mav_autopilot) {
    case MAV_AUTOPILOT_PX4:
      return Autopilot::Px4;
    case MAV_AUTOPILOT_ARDUPILOTMEGA:
      return Autopilot::ArduPilot;
    default:
      return Autopilot::Other;
  }
}

std::optional<GnssFix> gnss_fix_from(uint8_t fix_type) {
  switch (fix_type) {
    case GPS_FIX_TYPE_NO_GPS:
    case GPS_FIX_TYPE_NO_FIX:
      return GnssFix::NoFix;
    case GPS_FIX_TYPE_2D_FIX:
      return GnssFix::Fix2D;
    case GPS_FIX_TYPE_3D_FIX:
    case GPS_FIX_TYPE_STATIC:
    case GPS_FIX_TYPE_PPP:
      return GnssFix::Fix3D;
    case GPS_FIX_TYPE_DGPS:
      return GnssFix::Dgps;
    case GPS_FIX_TYPE_RTK_FLOAT:
      return GnssFix::RtkFloat;
    case GPS_FIX_TYPE_RTK_FIXED:
      return GnssFix::RtkFixed;
    default:
      return std::nullopt;
  }
}

std::optional<LandedState> landed_from(uint8_t s) {
  switch (s) {
    case MAV_LANDED_STATE_ON_GROUND:
      return LandedState::OnGround;
    case MAV_LANDED_STATE_IN_AIR:
      return LandedState::InAir;
    case MAV_LANDED_STATE_TAKEOFF:
      return LandedState::TakingOff;
    case MAV_LANDED_STATE_LANDING:
      return LandedState::Landing;
    default:
      return std::nullopt;
  }
}

std::string format_version(uint32_t v) {
  // flight_sw_version is major.minor.patch.type, one byte each, major first.
  return std::to_string(v >> 24) + "." + std::to_string((v >> 16) & 0xFF) +
         "." + std::to_string((v >> 8) & 0xFF);
}

}  // namespace

Changed VehicleModel::apply(const mavlink_message_t& msg) {
  state_discrete_ = false;
  health_discrete_ = false;

  // Until the first autopilot HEARTBEAT locks the model to a vehicle, nothing
  // else is applied, so another system's telemetry cannot leak in. After the
  // lock only the locked (sysid, compid) is used; payload components such as
  // a camera or companion share the sysid but are ignored for now.
  if (!adapter_ && msg.msgid != MAVLINK_MSG_ID_HEARTBEAT) return Changed::None;
  if (adapter_ && (msg.sysid != info_.mav_sysid ||
                   msg.compid != info_.mav_compid)) {
    return Changed::None;
  }

  switch (msg.msgid) {
    case MAVLINK_MSG_ID_HEARTBEAT:
      return on_heartbeat(msg);
    case MAVLINK_MSG_ID_GLOBAL_POSITION_INT:
      return on_global_position(msg);
    case MAVLINK_MSG_ID_ATTITUDE_QUATERNION:
      return on_attitude(msg);
    case MAVLINK_MSG_ID_SYS_STATUS:
      return on_sys_status(msg);
    case MAVLINK_MSG_ID_BATTERY_STATUS:
      return on_battery_status(msg);
    case MAVLINK_MSG_ID_GPS_RAW_INT:
      return on_gps_raw(msg);
    case MAVLINK_MSG_ID_EXTENDED_SYS_STATE:
      return on_extended_sys_state(msg);
    case MAVLINK_MSG_ID_SYSTEM_TIME:
      return on_system_time(msg);
    case MAVLINK_MSG_ID_AUTOPILOT_VERSION:
      return on_autopilot_version(msg);
    default:
      return Changed::None;
  }
}

Changed VehicleModel::on_heartbeat(const mavlink_message_t& msg) {
  mavlink_heartbeat_t h;
  mavlink_msg_heartbeat_decode(&msg, &h);
  if (h.type == MAV_TYPE_GCS || msg.compid != MAV_COMP_ID_AUTOPILOT1) {
    return Changed::None;
  }

  Changed changed = Changed::State;
  if (!adapter_) {
    adapter_ = make_adapter(h.autopilot);
    info_.mav_sysid = static_cast<uint8_t>(msg.sysid);
    info_.mav_compid = msg.compid;
    changed |= Changed::Info;
  }

  const InfoData before = info_;
  info_.autopilot = autopilot_from(h.autopilot);
  info_.vehicle_type = vehicle_type_from(h.type);
  if (info_.autopilot != before.autopilot ||
      info_.vehicle_type != before.vehicle_type) {
    changed |= Changed::Info;
  }

  const bool armed = (h.base_mode & MAV_MODE_FLAG_SAFETY_ARMED) != 0;
  state_discrete_ |= assign(state_.armed, std::optional<bool>{armed});
  state_discrete_ |= assign(
      state_.mode,
      std::optional<ModeInfo>{adapter_->decode_mode(h.base_mode, h.custom_mode)});
  state_discrete_ |=
      assign(state_.system_status, std::optional<uint8_t>{h.system_status});
  return changed;
}

Changed VehicleModel::on_global_position(const mavlink_message_t& msg) {
  mavlink_global_position_int_t g;
  mavlink_msg_global_position_int_decode(&msg, &g);
  pose_.time_boot_ms = g.time_boot_ms;
  pose_.lat_deg = static_cast<double>(g.lat) * 1e-7;
  pose_.lon_deg = static_cast<double>(g.lon) * 1e-7;
  pose_.alt_msl_m = static_cast<double>(g.alt) / 1000.0;
  pose_.alt_rel_home_m = static_cast<double>(g.relative_alt) / 1000.0;
  pose_.vel_n_mps = static_cast<float>(g.vx) / 100.0f;
  pose_.vel_e_mps = static_cast<float>(g.vy) / 100.0f;
  pose_.vel_d_mps = static_cast<float>(g.vz) / 100.0f;
  pose_.heading_deg =
      g.hdg == UINT16_MAX ? kNaNf : static_cast<float>(g.hdg) / 100.0f;
  return Changed::Pose | note_boot_time(g.time_boot_ms);
}

Changed VehicleModel::on_attitude(const mavlink_message_t& msg) {
  mavlink_attitude_quaternion_t q;
  mavlink_msg_attitude_quaternion_decode(&msg, &q);
  pose_.time_boot_ms = q.time_boot_ms;
  pose_.q_w = q.q1;
  pose_.q_x = q.q2;
  pose_.q_y = q.q3;
  pose_.q_z = q.q4;
  return Changed::Pose | note_boot_time(q.time_boot_ms);
}

Changed VehicleModel::on_sys_status(const mavlink_message_t& msg) {
  mavlink_sys_status_t s;
  mavlink_msg_sys_status_decode(&msg, &s);
  health_.battery_voltage_v =
      s.voltage_battery == UINT16_MAX
          ? kNaNf
          : static_cast<float>(s.voltage_battery) / 1000.0f;
  health_.battery_current_a =
      s.current_battery == -1 ? kNaNf
                              : static_cast<float>(s.current_battery) / 100.0f;
  health_.battery_remaining_pct =
      s.battery_remaining == -1 ? kNaNf
                                : static_cast<float>(s.battery_remaining);

  // Copies, because the message struct is packed and cannot be bound to a
  // reference.
  const uint32_t present = s.onboard_control_sensors_present;
  const uint32_t enabled = s.onboard_control_sensors_enabled;
  const uint32_t healthy = s.onboard_control_sensors_health;
  health_discrete_ |=
      assign(health_.sensors_present, std::optional<uint32_t>{present});
  health_discrete_ |=
      assign(health_.sensors_enabled, std::optional<uint32_t>{enabled});
  health_discrete_ |=
      assign(health_.sensors_healthy, std::optional<uint32_t>{healthy});

  const uint32_t ahrs = MAV_SYS_STATUS_AHRS;
  const bool ok = (enabled & ahrs) && (healthy & ahrs);
  health_discrete_ |= assign(health_.estimator_ok, std::optional<bool>{ok});
  return Changed::Health;
}

Changed VehicleModel::on_battery_status(const mavlink_message_t& msg) {
  mavlink_battery_status_t b;
  mavlink_msg_battery_status_decode(&msg, &b);
  // Only the first battery; a second pack would otherwise fight over the field.
  if (b.id != 0) return Changed::None;
  health_.battery_time_remaining_s =
      b.time_remaining == 0 ? kNaNf : static_cast<float>(b.time_remaining);
  return Changed::Health;
}

Changed VehicleModel::on_gps_raw(const mavlink_message_t& msg) {
  mavlink_gps_raw_int_t g;
  mavlink_msg_gps_raw_int_decode(&msg, &g);
  health_.gnss_hdop =
      g.eph == UINT16_MAX ? kNaNf : static_cast<float>(g.eph) / 100.0f;
  health_discrete_ |=
      assign(health_.gnss_fix, gnss_fix_from(g.fix_type));
  std::optional<uint8_t> sats;
  if (g.satellites_visible != 255) sats = g.satellites_visible;
  health_discrete_ |= assign(health_.gnss_satellites, sats);
  return Changed::Health;
}

Changed VehicleModel::on_extended_sys_state(const mavlink_message_t& msg) {
  mavlink_extended_sys_state_t e;
  mavlink_msg_extended_sys_state_decode(&msg, &e);
  state_discrete_ |= assign(state_.landed,
                            landed_from(e.landed_state));
  return Changed::State;
}

Changed VehicleModel::on_system_time(const mavlink_message_t& msg) {
  mavlink_system_time_t t;
  mavlink_msg_system_time_decode(&msg, &t);
  // First, so a reboot drops the old offset even when this message has none
  // to replace it with.
  const Changed reboot = note_boot_time(t.time_boot_ms);
  // A vehicle with no clock source sends 0; keep whatever offset we had.
  if (t.time_unix_usec == 0) return reboot;
  time_offset_us_ = static_cast<int64_t>(t.time_unix_usec) -
                    static_cast<int64_t>(t.time_boot_ms) * 1000;
  have_time_offset_ = true;
  return Changed::Time | reboot;
}

Changed VehicleModel::note_boot_time(uint32_t t) {
  if (max_boot_ms_ && *max_boot_ms_ > t &&
      *max_boot_ms_ - t > kRebootBackstepMs) {
    // The old offset would date the new boot's stamps by the old uptime.
    have_time_offset_ = false;
    time_offset_us_ = 0;
    max_boot_ms_ = t;
    return Changed::Reboot;
  }
  if (!max_boot_ms_ || t > *max_boot_ms_) max_boot_ms_ = t;
  return Changed::None;
}

Changed VehicleModel::on_autopilot_version(const mavlink_message_t& msg) {
  mavlink_autopilot_version_t a;
  mavlink_msg_autopilot_version_decode(&msg, &a);
  if (a.flight_sw_version != 0) {
    info_.firmware_version = format_version(a.flight_sw_version);
  }
  info_.capabilities = a.capabilities;
  return Changed::Info;
}

uint64_t VehicleModel::source_time_us(uint32_t time_boot_ms) const {
  if (!have_time_offset_) return 0;
  const int64_t t = time_offset_us_ + static_cast<int64_t>(time_boot_ms) * 1000;
  return t > 0 ? static_cast<uint64_t>(t) : 0;
}

void VehicleModel::reset_for_resetup() {
  info_.firmware_version.reset();
  info_.capabilities.reset();
  have_time_offset_ = false;
  time_offset_us_ = 0;
  max_boot_ms_.reset();
}

}  // namespace skyrelay::vehicle
