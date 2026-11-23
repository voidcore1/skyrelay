#include <gtest/gtest.h>

#include <cmath>

#include "skyrelay/common/version.hpp"
#include "skyrelay/dds/gen/v1_valid_bits.hpp"
#include "skyrelay/dds/mapping.hpp"

using namespace skyrelay;
using namespace skyrelay::dds;
using namespace skyrelay::vehicle;
namespace valid = skyrelay::v1::valid;

TEST(Mapping, PoseValidMaskFollowsNaN) {
  PoseData d;
  d.lat_deg = 47;
  d.lon_deg = 8;  // rest NaN
  auto s = to_idl(1, d, {}, 0);
  EXPECT_EQ(s.header().valid_mask(),
            valid::Pose::lat_deg | valid::Pose::lon_deg);
  EXPECT_DOUBLE_EQ(s.lat_deg(), 47);
  EXPECT_TRUE(std::isnan(s.alt_msl_m()));
}

TEST(Mapping, HealthOptionalBits) {
  HealthData h;
  h.gnss_satellites = 12;
  auto s = to_idl(1, h, {}, 0);
  EXPECT_TRUE(s.header().valid_mask() & valid::Health::gnss_satellites);
  EXPECT_FALSE(s.header().valid_mask() & valid::Health::gnss_fix);
  EXPECT_EQ(s.gnss_fix(), v1::GnssFix::GNSS_FIX_UNKNOWN);
  EXPECT_EQ(s.gnss_satellites(), 12);
  EXPECT_EQ(s.sensors_present(), 0u);
}

TEST(Mapping, StampsCopied) {
  auto s = to_idl(4, PoseData{}, {.t_source_us = 7, .t_bridge_ns = 9}, 5);
  EXPECT_EQ(s.vehicle_id(), 4u);
  EXPECT_EQ(s.header().seq(), 5u);
  EXPECT_EQ(s.header().t_source_us(), 7u);
  EXPECT_EQ(s.header().t_bridge_ns(), 9u);
}

TEST(Mapping, EmptyStateHasNoBits) {
  auto s = to_idl(1, StateData{}, {}, 0);
  EXPECT_EQ(s.header().valid_mask(), 0u);
  EXPECT_EQ(s.mode(), v1::FlightMode::FLIGHT_MODE_UNKNOWN);
  EXPECT_EQ(s.landed(), v1::LandedState::LANDED_STATE_UNKNOWN);
}

TEST(Mapping, StateFullAndRawModeBit) {
  StateData d;
  d.armed = true;
  d.mode = ModeInfo{FlightMode::Offboard, "OFFBOARD"};
  d.landed = LandedState::InAir;
  d.system_status = 4;
  auto s = to_idl(1, d, {}, 0);
  EXPECT_EQ(s.header().valid_mask(),
            valid::VehicleState::armed | valid::VehicleState::mode |
                valid::VehicleState::mode_raw | valid::VehicleState::landed |
                valid::VehicleState::system_status);
  EXPECT_TRUE(s.armed());
  EXPECT_EQ(s.mode(), v1::FlightMode::FLIGHT_MODE_OFFBOARD);
  EXPECT_EQ(s.mode_raw(), "OFFBOARD");
  EXPECT_EQ(s.landed(), v1::LandedState::LANDED_STATE_IN_AIR);

  d.mode = ModeInfo{FlightMode::Hold, ""};
  s = to_idl(1, d, {}, 0);
  EXPECT_TRUE(s.header().valid_mask() & valid::VehicleState::mode);
  EXPECT_FALSE(s.header().valid_mask() & valid::VehicleState::mode_raw);
}

TEST(Mapping, InfoUnknownFirmwareClearsBit) {
  InfoData d;
  d.autopilot = Autopilot::Px4;
  d.vehicle_type = VehicleType::Vtol;
  d.mav_sysid = 3;
  d.mav_compid = 1;
  auto s = to_idl(7, d, {}, 0);
  EXPECT_EQ(s.header().valid_mask(),
            valid::VehicleInfo::autopilot | valid::VehicleInfo::vehicle_type |
                valid::VehicleInfo::mav_sysid |
                valid::VehicleInfo::mav_compid |
                valid::VehicleInfo::contract_version);
  EXPECT_EQ(s.firmware_version(), "");
  EXPECT_EQ(s.contract_version(), std::string(kContractVersion));
  EXPECT_EQ(s.autopilot(), v1::AutopilotKind::AUTOPILOT_PX4);
  EXPECT_EQ(s.vehicle_type(), v1::VehicleType::VEHICLE_TYPE_VTOL);

  d.firmware_version = "1.17.0";
  d.capabilities = 0x5;
  s = to_idl(7, d, {}, 0);
  EXPECT_TRUE(s.header().valid_mask() & valid::VehicleInfo::firmware_version);
  EXPECT_TRUE(s.header().valid_mask() & valid::VehicleInfo::capabilities);
  EXPECT_EQ(s.firmware_version(), "1.17.0");
  EXPECT_EQ(s.capabilities(), 5u);
}

TEST(Mapping, LinkStatusAlwaysFullAndSigningOff) {
  LinkStatusData d;
  d.state = mav::LinkState::Degraded;
  d.state_since_ns = 99;
  d.stats = {12.5, 3.0, std::chrono::milliseconds(40)};
  auto s = to_idl(2, d, {}, 0);
  EXPECT_EQ(s.header().valid_mask(),
            valid::LinkStatus::state | valid::LinkStatus::state_since_ns |
                valid::LinkStatus::rx_rate_hz | valid::LinkStatus::loss_pct |
                valid::LinkStatus::last_rx_age_ms |
                valid::LinkStatus::signing);
  EXPECT_EQ(s.state(), v1::LinkState::LINK_DEGRADED);
  EXPECT_EQ(s.state_since_ns(), 99u);
  EXPECT_FLOAT_EQ(s.rx_rate_hz(), 12.5f);
  EXPECT_EQ(s.last_rx_age_ms(), 40u);
  EXPECT_EQ(s.signing(), v1::SigningState::SIGNING_OFF);
}

TEST(Mapping, EnumConversionsCoverEveryValue) {
  const GnssFix fixes[] = {GnssFix::Unknown, GnssFix::NoFix,   GnssFix::Fix2D,
                           GnssFix::Fix3D,   GnssFix::Dgps,    GnssFix::RtkFloat,
                           GnssFix::RtkFixed};
  for (auto f : fixes) {
    HealthData h;
    h.gnss_fix = f;
    EXPECT_EQ(static_cast<int>(to_idl(1, h, {}, 0).gnss_fix()),
              static_cast<int>(f));
  }
  for (uint8_t m = 0; m <= static_cast<uint8_t>(FlightMode::Offboard); ++m) {
    StateData d;
    d.mode = ModeInfo{static_cast<FlightMode>(m), ""};
    EXPECT_EQ(static_cast<int>(to_idl(1, d, {}, 0).mode()), m);
  }
}
