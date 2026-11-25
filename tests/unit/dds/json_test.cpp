#include <cmath>

#include <gtest/gtest.h>

#include <skyrelay/dds/gen/v1_json.hpp>
#include <skyrelay/dds/gen/v1_valid_bits.hpp>

TEST(Json, PoseNaNBecomesNull) {
  skyrelay::v1::Pose p;
  p.vehicle_id(2);
  p.lat_deg(std::nan(""));
  p.lon_deg(8.5);
  p.header().seq(7);
  const auto j = to_json_value(p);
  EXPECT_TRUE(j["lat_deg"].is_null());
  EXPECT_EQ(j["vehicle_id"], 2);
  EXPECT_DOUBLE_EQ(j["lon_deg"].get<double>(), 8.5);
  EXPECT_EQ(j["header"]["seq"], 7);
}

TEST(Json, EnumAsLiteralName) {
  skyrelay::v1::LinkStatus s;
  s.state(skyrelay::v1::LinkState::LINK_DEGRADED);
  const auto j = to_json_value(s);
  EXPECT_EQ(j["state"], "LINK_DEGRADED");
}

TEST(Json, StringAndBoolFields) {
  skyrelay::v1::VehicleState s;
  s.armed(true);
  s.mode_raw("AUTO.LOITER");
  const auto j = to_json_value(s);
  EXPECT_EQ(j["armed"], true);
  EXPECT_EQ(j["mode_raw"], "AUTO.LOITER");
}

TEST(ValidBits, PoseHeadingIsBit11) {
  EXPECT_EQ(skyrelay::v1::valid::Pose::heading_deg, 1ull << 11);
  EXPECT_EQ(skyrelay::v1::valid::Pose::lat_deg, 1ull << 0);
}

TEST(ValidBits, SameFieldNameInDifferentStructsDoesNotClash) {
  EXPECT_EQ(skyrelay::v1::valid::LinkStatus::state, 1ull << 0);
  EXPECT_EQ(skyrelay::v1::valid::VehicleState::armed, 1ull << 0);
  EXPECT_EQ(skyrelay::v1::valid::LinkStatus::signing, 1ull << 5);
}
