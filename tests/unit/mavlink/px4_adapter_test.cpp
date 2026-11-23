#include <gtest/gtest.h>

#include <algorithm>

#include "skyrelay/vehicle/adapter.hpp"

using namespace skyrelay::vehicle;

namespace {

constexpr uint8_t kCustom = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;

uint32_t px4_mode(uint8_t main, uint8_t sub) {
  return (static_cast<uint32_t>(main) << 16) |
         (static_cast<uint32_t>(sub) << 24);
}

}  // namespace

TEST(Px4Adapter, DecodesModeTable) {
  auto a = make_adapter(MAV_AUTOPILOT_PX4);
  ASSERT_TRUE(a);
  EXPECT_EQ(a->kind(), Autopilot::Px4);

  struct Row {
    uint8_t main, sub;
    const char* raw;
    FlightMode mode;
  };
  const Row rows[] = {
      {1, 0, "MANUAL", FlightMode::Manual},
      {2, 0, "ALTCTL", FlightMode::AltitudeHold},
      {3, 0, "POSCTL", FlightMode::PositionHold},
      {4, 2, "AUTO.TAKEOFF", FlightMode::Takeoff},
      {4, 3, "AUTO.LOITER", FlightMode::Hold},
      {4, 4, "AUTO.MISSION", FlightMode::Mission},
      {4, 5, "AUTO.RTL", FlightMode::Return},
      {4, 6, "AUTO.LAND", FlightMode::Land},
      {5, 0, "ACRO", FlightMode::Acro},
      {6, 0, "OFFBOARD", FlightMode::Offboard},
      {7, 0, "STABILIZED", FlightMode::Stabilized},
  };
  for (const auto& r : rows) {
    auto m = a->decode_mode(kCustom, px4_mode(r.main, r.sub));
    EXPECT_EQ(m.mode, r.mode) << r.raw;
    EXPECT_EQ(m.raw, r.raw);
  }
}

TEST(Px4Adapter, UnknownModeKeepsRawCode) {
  auto a = make_adapter(MAV_AUTOPILOT_PX4);
  auto m = a->decode_mode(kCustom, px4_mode(4, 9));
  EXPECT_EQ(m.mode, FlightMode::Unknown);
  EXPECT_EQ(m.raw, "PX4:4.9");
  m = a->decode_mode(kCustom, px4_mode(42, 0));
  EXPECT_EQ(m.mode, FlightMode::Unknown);
  EXPECT_EQ(m.raw, "PX4:42.0");
}

TEST(Px4Adapter, CustomModeFlagClearedIsUnknown) {
  auto a = make_adapter(MAV_AUTOPILOT_PX4);
  auto m = a->decode_mode(0, px4_mode(3, 0));
  EXPECT_EQ(m.mode, FlightMode::Unknown);
  EXPECT_EQ(m.raw, "");
}

TEST(Adapter, FactoryPicksGenericForOtherAutopilots) {
  EXPECT_EQ(make_adapter(MAV_AUTOPILOT_PX4)->kind(), Autopilot::Px4);
  EXPECT_EQ(make_adapter(MAV_AUTOPILOT_ARDUPILOTMEGA)->kind(),
            Autopilot::ArduPilot);
  EXPECT_EQ(make_adapter(MAV_AUTOPILOT_GENERIC)->kind(), Autopilot::Other);
  auto m = make_adapter(MAV_AUTOPILOT_GENERIC)->decode_mode(kCustom, 5);
  EXPECT_EQ(m.mode, FlightMode::Unknown);
}

TEST(Adapter, DefaultTelemetryIntervals) {
  auto v = make_adapter(MAV_AUTOPILOT_PX4)->telemetry_intervals();
  ASSERT_EQ(v.size(), 7u);
  auto hz = [&](uint32_t id) {
    auto it = std::find_if(v.begin(), v.end(),
                           [&](const auto& r) { return r.msg_id == id; });
    return it == v.end() ? -1.0 : it->hz;
  };
  EXPECT_EQ(hz(33), 20.0);
  EXPECT_EQ(hz(31), 50.0);
  EXPECT_EQ(hz(1), 2.0);
  EXPECT_EQ(hz(147), 2.0);
  EXPECT_EQ(hz(24), 2.0);
  EXPECT_EQ(hz(245), 5.0);
  EXPECT_EQ(hz(2), 1.0);
}
