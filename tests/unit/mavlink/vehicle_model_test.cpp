#include <gtest/gtest.h>

#include <cmath>

#include "skyrelay/vehicle/vehicle_model.hpp"

using namespace skyrelay::vehicle;

namespace {

constexpr uint8_t kSys = 1;
constexpr uint8_t kComp = MAV_COMP_ID_AUTOPILOT1;

mavlink_message_t heartbeat(uint8_t type, uint8_t autopilot, uint8_t base_mode,
                            uint32_t custom_mode = 0, uint8_t status = 3,
                            uint8_t comp = kComp) {
  mavlink_heartbeat_t h{};
  h.type = type;
  h.autopilot = autopilot;
  h.base_mode = base_mode;
  h.custom_mode = custom_mode;
  h.system_status = status;
  mavlink_message_t m;
  mavlink_msg_heartbeat_encode(kSys, comp, &m, &h);
  return m;
}

mavlink_message_t px4_heartbeat(uint8_t base_mode, uint32_t custom_mode = 0) {
  return heartbeat(MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_PX4, base_mode,
                   custom_mode);
}

mavlink_message_t gpi(int32_t lat, int32_t lon, int32_t alt, int32_t rel,
                      int16_t vx, int16_t vy, int16_t vz, uint16_t hdg) {
  mavlink_global_position_int_t g{};
  g.time_boot_ms = 5000;
  g.lat = lat;
  g.lon = lon;
  g.alt = alt;
  g.relative_alt = rel;
  g.vx = vx;
  g.vy = vy;
  g.vz = vz;
  g.hdg = hdg;
  mavlink_message_t m;
  mavlink_msg_global_position_int_encode(kSys, kComp, &m, &g);
  return m;
}

mavlink_message_t sys_status(uint32_t present, uint32_t enabled,
                             uint32_t health, uint16_t volt, int16_t cur,
                             int8_t rem) {
  mavlink_sys_status_t s{};
  s.onboard_control_sensors_present = present;
  s.onboard_control_sensors_enabled = enabled;
  s.onboard_control_sensors_health = health;
  s.voltage_battery = volt;
  s.current_battery = cur;
  s.battery_remaining = rem;
  mavlink_message_t m;
  mavlink_msg_sys_status_encode(kSys, kComp, &m, &s);
  return m;
}

mavlink_message_t gps(uint8_t fix, uint16_t eph, uint8_t sats) {
  mavlink_gps_raw_int_t g{};
  g.fix_type = fix;
  g.eph = eph;
  g.satellites_visible = sats;
  mavlink_message_t m;
  mavlink_msg_gps_raw_int_encode(kSys, kComp, &m, &g);
  return m;
}

mavlink_message_t system_time(uint64_t unix_us, uint32_t boot_ms) {
  mavlink_system_time_t t{};
  t.time_unix_usec = unix_us;
  t.time_boot_ms = boot_ms;
  mavlink_message_t m;
  mavlink_msg_system_time_encode(kSys, kComp, &m, &t);
  return m;
}

mavlink_message_t gpi_at(uint32_t time_boot_ms) {
  mavlink_global_position_int_t g{};
  g.time_boot_ms = time_boot_ms;
  mavlink_message_t m;
  mavlink_msg_global_position_int_encode(kSys, kComp, &m, &g);
  return m;
}

mavlink_message_t attitude_at(uint32_t time_boot_ms) {
  mavlink_attitude_quaternion_t q{};
  q.time_boot_ms = time_boot_ms;
  q.q1 = 1.0f;
  mavlink_message_t m;
  mavlink_msg_attitude_quaternion_encode(kSys, kComp, &m, &q);
  return m;
}

// A model already locked to the vehicle (sysid 1, compid 1).
VehicleModel locked() {
  VehicleModel v;
  v.apply(px4_heartbeat(0));
  return v;
}

}  // namespace

TEST(VehicleModel, DecodesGlobalPositionInt) {
  auto v = locked();
  auto c = v.apply(gpi(473977420, 85455940, 488123, 10123, 150, -20, 5, 9000));
  EXPECT_EQ(c, Changed::Pose);
  const auto& p = v.pose();
  EXPECT_DOUBLE_EQ(p.lat_deg, 47.397742);
  EXPECT_DOUBLE_EQ(p.lon_deg, 8.545594);
  EXPECT_DOUBLE_EQ(p.alt_msl_m, 488.123);
  EXPECT_DOUBLE_EQ(p.alt_rel_home_m, 10.123);
  EXPECT_FLOAT_EQ(p.vel_n_mps, 1.5f);
  EXPECT_FLOAT_EQ(p.vel_e_mps, -0.2f);
  EXPECT_FLOAT_EQ(p.vel_d_mps, 0.05f);
  EXPECT_FLOAT_EQ(p.heading_deg, 90.0f);
  EXPECT_EQ(p.time_boot_ms, 5000u);
  EXPECT_TRUE(std::isnan(p.q_w));
}

TEST(VehicleModel, HeadingSentinelIsNaN) {
  auto v = locked();
  v.apply(gpi(1, 2, 3, 4, 0, 0, 0, UINT16_MAX));
  EXPECT_TRUE(std::isnan(v.pose().heading_deg));
}

TEST(VehicleModel, AttitudeQuaternion) {
  auto v = locked();
  mavlink_attitude_quaternion_t q{};
  q.time_boot_ms = 777;
  q.q1 = 1.0f;
  q.q2 = 0.1f;
  q.q3 = 0.2f;
  q.q4 = 0.3f;
  mavlink_message_t m;
  mavlink_msg_attitude_quaternion_encode(kSys, kComp, &m, &q);
  EXPECT_EQ(v.apply(m), Changed::Pose);
  EXPECT_FLOAT_EQ(v.pose().q_w, 1.0f);
  EXPECT_FLOAT_EQ(v.pose().q_x, 0.1f);
  EXPECT_FLOAT_EQ(v.pose().q_y, 0.2f);
  EXPECT_FLOAT_EQ(v.pose().q_z, 0.3f);
  EXPECT_TRUE(std::isnan(v.pose().lat_deg));
}

TEST(VehicleModel, DecodesSysStatus) {
  auto v = locked();
  auto c = v.apply(sys_status(0xFF, 0xFF, 0x0F, 12600, 1550, 80));
  EXPECT_EQ(c, Changed::Health);
  EXPECT_FLOAT_EQ(v.health().battery_voltage_v, 12.6f);
  EXPECT_FLOAT_EQ(v.health().battery_current_a, 15.5f);
  EXPECT_FLOAT_EQ(v.health().battery_remaining_pct, 80.0f);
  EXPECT_EQ(v.health().sensors_present, 0xFFu);
  EXPECT_EQ(v.health().sensors_enabled, 0xFFu);
  EXPECT_EQ(v.health().sensors_healthy, 0x0Fu);
}

TEST(VehicleModel, SysStatusSentinels) {
  auto v = locked();
  v.apply(sys_status(0, 0, 0, UINT16_MAX, -1, -1));
  EXPECT_TRUE(std::isnan(v.health().battery_voltage_v));
  EXPECT_TRUE(std::isnan(v.health().battery_current_a));
  EXPECT_TRUE(std::isnan(v.health().battery_remaining_pct));
}

TEST(VehicleModel, SysStatusSentinelClearsEarlierValue) {
  auto v = locked();
  v.apply(sys_status(0, 0, 0, 12600, 100, 50));
  v.apply(sys_status(0, 0, 0, UINT16_MAX, -1, -1));
  EXPECT_TRUE(std::isnan(v.health().battery_voltage_v));
  EXPECT_TRUE(std::isnan(v.health().battery_remaining_pct));
}

TEST(VehicleModel, EstimatorOkNeedsAhrsEnabledAndHealthy) {
  auto v = locked();
  const uint32_t ahrs = MAV_SYS_STATUS_AHRS;
  v.apply(sys_status(ahrs, ahrs, ahrs, 0, 0, 0));
  EXPECT_EQ(v.health().estimator_ok, true);
  v.apply(sys_status(ahrs, ahrs, 0, 0, 0, 0));
  EXPECT_EQ(v.health().estimator_ok, false);
  v.apply(sys_status(ahrs, 0, ahrs, 0, 0, 0));
  EXPECT_EQ(v.health().estimator_ok, false);
}

TEST(VehicleModel, GpsSentinels) {
  auto v = locked();
  auto c = v.apply(gps(3, UINT16_MAX, 255));
  EXPECT_EQ(c, Changed::Health);
  EXPECT_TRUE(std::isnan(v.health().gnss_hdop));
  EXPECT_FALSE(v.health().gnss_satellites.has_value());
  EXPECT_EQ(v.health().gnss_fix, GnssFix::Fix3D);
}

TEST(VehicleModel, GpsValuesAndFixMapping) {
  auto v = locked();
  v.apply(gps(6, 120, 14));
  EXPECT_FLOAT_EQ(v.health().gnss_hdop, 1.2f);
  EXPECT_EQ(v.health().gnss_satellites, 14);
  EXPECT_EQ(v.health().gnss_fix, GnssFix::RtkFixed);
  v.apply(gps(0, 0, 0));
  EXPECT_EQ(v.health().gnss_fix, GnssFix::NoFix);
  v.apply(gps(2, 0, 0));
  EXPECT_EQ(v.health().gnss_fix, GnssFix::Fix2D);
  v.apply(gps(4, 0, 0));
  EXPECT_EQ(v.health().gnss_fix, GnssFix::Dgps);
  v.apply(gps(5, 0, 0));
  EXPECT_EQ(v.health().gnss_fix, GnssFix::RtkFloat);
  v.apply(gps(9, 0, 0));
  EXPECT_FALSE(v.health().gnss_fix.has_value());
}

TEST(VehicleModel, BatteryTimeRemaining) {
  auto v = locked();
  mavlink_battery_status_t b{};
  b.time_remaining = 600;
  mavlink_message_t m;
  mavlink_msg_battery_status_encode(kSys, kComp, &m, &b);
  EXPECT_EQ(v.apply(m), Changed::Health);
  EXPECT_FLOAT_EQ(v.health().battery_time_remaining_s, 600.0f);
  b.time_remaining = 0;
  mavlink_msg_battery_status_encode(kSys, kComp, &m, &b);
  v.apply(m);
  EXPECT_TRUE(std::isnan(v.health().battery_time_remaining_s));
}

TEST(VehicleModel, SourceTimeFromSystemTime) {
  auto v = locked();
  EXPECT_EQ(v.apply(system_time(1'700'000'000'000'000ull, 10'000)),
            Changed::Time);
  EXPECT_EQ(v.source_time_us(12'000), 1'700'000'002'000'000ull);
}

TEST(VehicleModel, SourceTimeUnknownIsZero) {
  auto v = locked();
  EXPECT_EQ(v.source_time_us(12'000), 0u);
  v.apply(system_time(0, 10'000));
  EXPECT_EQ(v.source_time_us(12'000), 0u);
}

TEST(VehicleModel, TimeBootGoingBackwardsIsAReboot) {
  auto v = locked();
  v.apply(system_time(1'700'000'000'000'000ull, 600'000));
  EXPECT_EQ(v.apply(gpi_at(600'020)), Changed::Pose);
  // A frame up to a second older (reordering, a late stream) is not a reboot.
  EXPECT_EQ(v.apply(gpi_at(599'100)), Changed::Pose);
  EXPECT_EQ(v.source_time_us(600'000), 1'700'000'000'000'000ull);

  const Changed c = v.apply(gpi_at(300));
  EXPECT_EQ(c, Changed::Pose | Changed::Reboot);
  EXPECT_EQ(v.source_time_us(300), 0u);

  // The new boot moves forward again: one reboot, not one per frame.
  EXPECT_EQ(v.apply(gpi_at(350)), Changed::Pose);
  EXPECT_EQ(v.apply(attitude_at(360)), Changed::Pose);
  EXPECT_EQ(v.source_time_us(350), 0u);
  v.apply(system_time(1'700'000'700'000'000ull, 400));
  EXPECT_EQ(v.source_time_us(400), 1'700'000'700'000'000ull);
}

TEST(VehicleModel, RebootSeenInSystemTimeAndAttitude) {
  auto v = locked();
  v.apply(system_time(1'700'000'000'000'000ull, 600'000));
  // Rebooted with no clock source: unix 0 must not leave the old offset.
  EXPECT_EQ(v.apply(system_time(0, 300)), Changed::Reboot);
  EXPECT_EQ(v.source_time_us(300), 0u);

  v.apply(attitude_at(700'000));
  EXPECT_EQ(v.apply(attitude_at(200)), Changed::Pose | Changed::Reboot);

  // A SYSTEM_TIME that reveals the reboot also brings the new offset.
  v.apply(system_time(1'700'000'000'000'000ull, 900'000));
  EXPECT_EQ(v.apply(system_time(1'700'000'900'000'000ull, 100)),
            Changed::Time | Changed::Reboot);
  EXPECT_EQ(v.source_time_us(100), 1'700'000'900'000'000ull);
}

TEST(VehicleModel, ResetForResetupForgetsBootTime) {
  auto v = locked();
  v.apply(gpi_at(600'000));
  v.reset_for_resetup();
  // The reconnect already re-learns everything; no second reboot on top.
  EXPECT_EQ(v.apply(gpi_at(300)), Changed::Pose);
}

TEST(VehicleModel, FirmwareVersionFormatted) {
  VehicleModel v;
  v.apply(px4_heartbeat(0));
  mavlink_autopilot_version_t a{};
  a.flight_sw_version = 0x01110000;
  a.capabilities = 0x1234;
  mavlink_message_t m;
  mavlink_msg_autopilot_version_encode(kSys, kComp, &m, &a);
  EXPECT_EQ(v.apply(m), Changed::Info);
  EXPECT_EQ(v.info().firmware_version, "1.17.0");
  EXPECT_EQ(v.info().capabilities, 0x1234u);
}

TEST(VehicleModel, HeartbeatBuildsStateAndInfo) {
  VehicleModel v;
  EXPECT_EQ(v.info().autopilot, Autopilot::Unknown);
  auto c = v.apply(px4_heartbeat(
      MAV_MODE_FLAG_CUSTOM_MODE_ENABLED | MAV_MODE_FLAG_SAFETY_ARMED,
      (3u << 16)));
  EXPECT_TRUE(has(c, Changed::State));
  EXPECT_TRUE(has(c, Changed::Info));
  EXPECT_EQ(v.state().armed, true);
  ASSERT_TRUE(v.state().mode);
  EXPECT_EQ(v.state().mode->mode, FlightMode::PositionHold);
  EXPECT_EQ(v.state().mode->raw, "POSCTL");
  EXPECT_EQ(v.state().system_status, 3);
  EXPECT_EQ(v.info().autopilot, Autopilot::Px4);
  EXPECT_EQ(v.info().vehicle_type, VehicleType::Multirotor);
  EXPECT_EQ(v.info().mav_sysid, kSys);
  EXPECT_EQ(v.info().mav_compid, kComp);

  c = v.apply(px4_heartbeat(MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, (3u << 16)));
  EXPECT_EQ(c, Changed::State);
  EXPECT_EQ(v.state().armed, false);
}

TEST(VehicleModel, GcsAndOtherComponentHeartbeatsDoNotSelectAdapter) {
  VehicleModel v;
  EXPECT_EQ(v.apply(heartbeat(MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID, 0, 0, 0)),
            Changed::None);
  EXPECT_EQ(v.apply(heartbeat(MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_PX4, 0, 0, 3,
                              MAV_COMP_ID_ONBOARD_COMPUTER)),
            Changed::None);
  EXPECT_EQ(v.info().autopilot, Autopilot::Unknown);
  EXPECT_FALSE(v.state().armed.has_value());
  v.apply(px4_heartbeat(0));
  EXPECT_EQ(v.info().autopilot, Autopilot::Px4);
}

TEST(VehicleModel, ExtendedSysStateLanded) {
  auto v = locked();
  mavlink_extended_sys_state_t e{};
  e.landed_state = MAV_LANDED_STATE_IN_AIR;
  mavlink_message_t m;
  mavlink_msg_extended_sys_state_encode(kSys, kComp, &m, &e);
  EXPECT_EQ(v.apply(m), Changed::State);
  EXPECT_EQ(v.state().landed, LandedState::InAir);
  EXPECT_TRUE(v.state_changed_discrete());
  e.landed_state = MAV_LANDED_STATE_UNDEFINED;
  mavlink_msg_extended_sys_state_encode(kSys, kComp, &m, &e);
  v.apply(m);
  EXPECT_FALSE(v.state().landed.has_value());
}

TEST(VehicleModel, DiscreteChangeFlags) {
  VehicleModel v;
  v.apply(px4_heartbeat(0));
  EXPECT_TRUE(v.state_changed_discrete());  // first sight counts as a change
  v.apply(px4_heartbeat(0));
  EXPECT_FALSE(v.state_changed_discrete());
  v.apply(px4_heartbeat(MAV_MODE_FLAG_SAFETY_ARMED));
  EXPECT_TRUE(v.state_changed_discrete());
  v.apply(px4_heartbeat(MAV_MODE_FLAG_SAFETY_ARMED));
  EXPECT_FALSE(v.state_changed_discrete());

  v.apply(sys_status(1, 1, 1, 12000, 0, 50));
  EXPECT_TRUE(v.health_changed_discrete());
  v.apply(sys_status(1, 1, 1, 11900, 0, 49));
  EXPECT_FALSE(v.health_changed_discrete());
  v.apply(sys_status(1, 1, 3, 11900, 0, 49));
  EXPECT_TRUE(v.health_changed_discrete());

  // A message that is ignored reports no change.
  v.apply(px4_heartbeat(0));
  EXPECT_TRUE(v.state_changed_discrete());
  v.apply(gpi(1, 2, 3, 4, 0, 0, 0, 0));
  EXPECT_FALSE(v.state_changed_discrete());
  EXPECT_FALSE(v.health_changed_discrete());
}

TEST(VehicleModel, ResetForResetupKeepsAdapterAndClearsLearnedInfo) {
  VehicleModel v;
  v.apply(px4_heartbeat(0));
  mavlink_autopilot_version_t a{};
  a.flight_sw_version = 0x01110000;
  a.capabilities = 7;
  mavlink_message_t m;
  mavlink_msg_autopilot_version_encode(kSys, kComp, &m, &a);
  v.apply(m);
  v.apply(system_time(1'700'000'000'000'000ull, 10'000));
  v.reset_for_resetup();
  EXPECT_FALSE(v.info().firmware_version.has_value());
  EXPECT_FALSE(v.info().capabilities.has_value());
  EXPECT_EQ(v.source_time_us(12'000), 0u);
  EXPECT_EQ(v.info().autopilot, Autopilot::Px4);
  EXPECT_EQ(v.info().mav_sysid, kSys);
  // adapter still there: modes keep decoding
  v.apply(px4_heartbeat(MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, (7u << 16)));
  EXPECT_EQ(v.state().mode->mode, FlightMode::Stabilized);
}

TEST(VehicleModel, IgnoresTelemetryBeforeLock) {
  VehicleModel v;
  EXPECT_EQ(v.apply(gpi(1, 2, 3, 4, 0, 0, 0, 0)), Changed::None);
  EXPECT_TRUE(std::isnan(v.pose().lat_deg));
  v.apply(px4_heartbeat(0));
  EXPECT_EQ(v.apply(gpi(473977420, 85455940, 0, 0, 0, 0, 0, 0)), Changed::Pose);
  EXPECT_DOUBLE_EQ(v.pose().lat_deg, 47.397742);
}

TEST(VehicleModel, IgnoresOtherSysid) {
  auto v = locked();
  mavlink_global_position_int_t g{};
  g.lat = 100;
  mavlink_message_t m;
  mavlink_msg_global_position_int_encode(9, kComp, &m, &g);
  EXPECT_EQ(v.apply(m), Changed::None);
  EXPECT_TRUE(std::isnan(v.pose().lat_deg));
  EXPECT_FALSE(v.state_changed_discrete());
  EXPECT_FALSE(v.health_changed_discrete());
}

TEST(VehicleModel, IgnoresOtherComponentSameSysid) {
  auto v = locked();
  v.apply(system_time(1'700'000'000'000'000ull, 10'000));
  mavlink_system_time_t t{};
  t.time_unix_usec = 5'000'000'000'000'000ull;
  t.time_boot_ms = 1;
  mavlink_message_t m;
  mavlink_msg_system_time_encode(kSys, MAV_COMP_ID_CAMERA, &m, &t);
  EXPECT_EQ(v.apply(m), Changed::None);
  EXPECT_EQ(v.source_time_us(12'000), 1'700'000'002'000'000ull);

  mavlink_battery_status_t b{};
  b.time_remaining = 600;
  mavlink_msg_battery_status_encode(kSys, MAV_COMP_ID_CAMERA, &m, &b);
  EXPECT_EQ(v.apply(m), Changed::None);
  EXPECT_TRUE(std::isnan(v.health().battery_time_remaining_s));
}
