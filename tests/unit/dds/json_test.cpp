#include <cmath>
#include <string>

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

namespace {
using skyrelay::v1::JsonError;

nlohmann::json valid_pose_json() {
  skyrelay::v1::Pose p;
  p.vehicle_id(3);
  p.lat_deg(47.1);
  return to_json_value(p);
}

// Mutates a valid Pose JSON and expects a JsonError starting with `prefix`.
template <class Mutate>
void expect_rejects(Mutate mutate, const std::string& prefix) {
  auto j = valid_pose_json();
  mutate(j);
  skyrelay::v1::Pose out;
  try {
    skyrelay::v1::from_json_value(j, out);
    ADD_FAILURE() << "accepted, wanted: " << prefix;
  } catch (const JsonError& e) {
    EXPECT_EQ(std::string(e.what()).rfind(prefix, 0), 0u) << e.what();
  }
}
}  // namespace

TEST(Json, PoseRoundTripsThroughFromJson) {
  skyrelay::v1::Pose p;
  p.vehicle_id(3);
  p.lat_deg(47.1);
  p.q_w(std::nanf(""));
  skyrelay::v1::Pose q;
  skyrelay::v1::from_json_value(to_json_value(p), q);
  EXPECT_EQ(q.lat_deg(), 47.1);
  EXPECT_TRUE(std::isnan(q.q_w()));
  EXPECT_EQ(q.vehicle_id(), 3u);
}

TEST(Json, FromJsonRejects) {
  expect_rejects([](auto& j) { j["extra"] = 1; }, "extra: unexpected key");
  expect_rejects([](auto& j) { j.erase("q_w"); }, "q_w: missing");
  expect_rejects([](auto& j) { j["lat_deg"] = "1"; }, "lat_deg: expected a number or null");
  expect_rejects([](auto& j) { j["vehicle_id"] = -1; }, "vehicle_id: out of range");
  expect_rejects([](auto& j) { j["vehicle_id"] = 4294967296ull; }, "vehicle_id: out of range");
  expect_rejects([](auto& j) { j["header"]["seq"] = 1.5; }, "header.seq: expected an integer");
  expect_rejects([](auto& j) { j["header"]["extra"] = 1; }, "header.extra: unexpected key");
  expect_rejects([](auto& j) { j = nlohmann::json::array(); }, "expected an object");
}

TEST(Json, EnumFromJsonRejectsUnknownLiteral) {
  skyrelay::v1::LinkState s;
  EXPECT_THROW(skyrelay::v1::from_json_value("LINK_GONE", s), JsonError);
  try {
    skyrelay::v1::from_json_value("LINK_GONE", s);
  } catch (const JsonError& e) {
    EXPECT_STREQ(e.what(), "unknown literal 'LINK_GONE'");
  }
  skyrelay::v1::from_json_value("LINK_DEGRADED", s);
  EXPECT_EQ(s, skyrelay::v1::LinkState::LINK_DEGRADED);
}

TEST(Json, FromJsonEnumPathAndType) {
  auto j = to_json_value(skyrelay::v1::LinkStatus{});
  j["state"] = "LINK_GONE";
  skyrelay::v1::LinkStatus out;
  try {
    skyrelay::v1::from_json_value(j, out);
    FAIL();
  } catch (const JsonError& e) {
    EXPECT_STREQ(e.what(), "state: unknown literal 'LINK_GONE'");
  }
  j["state"] = 3;
  EXPECT_THROW(skyrelay::v1::from_json_value(j, out), JsonError);
}

TEST(Json, FromJsonStringAndBool) {
  skyrelay::v1::VehicleState s;
  s.armed(true);
  s.mode_raw("AUTO");
  auto j = to_json_value(s);
  skyrelay::v1::VehicleState out;
  skyrelay::v1::from_json_value(j, out);
  EXPECT_TRUE(out.armed());
  EXPECT_EQ(out.mode_raw(), "AUTO");
  j["mode_raw"] = std::string(33, 'x');
  EXPECT_THROW(skyrelay::v1::from_json_value(j, out), JsonError);
  j["mode_raw"] = "ok";
  j["armed"] = 1;
  EXPECT_THROW(skyrelay::v1::from_json_value(j, out), JsonError);
}

TEST(ValidBits, PoseCoreMatchesContract) {
  using P = skyrelay::v1::valid::Pose;
  EXPECT_EQ(P::kCore, P::lat_deg | P::lon_deg | P::alt_msl_m | P::q_w | P::q_x | P::q_y | P::q_z |
                          P::vel_n_mps | P::vel_e_mps | P::vel_d_mps);
  EXPECT_EQ(P::kAll, (1ull << 12) - 1);
}
