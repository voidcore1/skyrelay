#include <cmath>
#include <limits>
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

TEST(Json, FromJsonRejectsTooManyWaypoints) {
  skyrelay::v1::CommandRequest r;
  auto j = to_json_value(r);
  j["waypoints"] = nlohmann::json::array();
  for (int i = 0; i < 501; ++i) j["waypoints"].push_back(to_json_value(skyrelay::v1::Waypoint{}));
  skyrelay::v1::CommandRequest out;
  try {
    skyrelay::v1::from_json_value(j, out);
    FAIL();
  } catch (const JsonError& e) {
    EXPECT_STREQ(e.what(), "waypoints: more than 500 elements");
  }
  j["waypoints"].erase(j["waypoints"].size() - 1);
  EXPECT_NO_THROW(skyrelay::v1::from_json_value(j, out));
  EXPECT_EQ(out.waypoints().size(), 500u);
}

TEST(Json, CommandIdIsDecimalString) {
  skyrelay::v1::CommandStatus s;
  s.command_id(18446744073709551557ull);
  EXPECT_EQ(skyrelay::v1::to_json_value(s)["command_id"], "18446744073709551557");
  skyrelay::v1::CommandStatus back;
  skyrelay::v1::from_json_value(skyrelay::v1::to_json_value(s), back);
  EXPECT_EQ(back.command_id(), 18446744073709551557ull);
}

TEST(Json, CommandRequestRoundTrip) {
  skyrelay::v1::CommandRequest r;
  r.vehicle_id(2);
  r.issuer_id(100);
  r.command_id(18446744073709551557ull);
  r.header().seq(5);
  r.kind(skyrelay::v1::CommandKind::COMMAND_KIND_MISSION_UPLOAD);
  r.params().lat_deg(47.5);
  r.params().heading_deg(std::nanf(""));
  r.deadline_ms(2500);
  r.confirm_token(18446744073709551615ull);
  r.operator_name("ops");
  std::vector<skyrelay::v1::Waypoint> wps(3);
  for (std::size_t i = 0; i < wps.size(); ++i) {
    wps[i].lat_deg(47.0 + static_cast<double>(i));
    wps[i].alt_rel_home_m(20.0f + static_cast<float>(i));
    wps[i].hold_s(std::nanf(""));
  }
  r.waypoints(wps);
  const auto j = skyrelay::v1::to_json_value(r);
  EXPECT_EQ(j["command_id"], "18446744073709551557");
  EXPECT_EQ(j["confirm_token"], "18446744073709551615");
  EXPECT_EQ(j["kind"], "COMMAND_KIND_MISSION_UPLOAD");
  EXPECT_TRUE(j["waypoints"][0]["hold_s"].is_null());
  skyrelay::v1::CommandRequest back;
  skyrelay::v1::from_json_value(j, back);
  EXPECT_EQ(back.command_id(), 18446744073709551557ull);
  EXPECT_EQ(back.confirm_token(), 18446744073709551615ull);
  EXPECT_EQ(back.kind(), skyrelay::v1::CommandKind::COMMAND_KIND_MISSION_UPLOAD);
  EXPECT_EQ(back.operator_name(), "ops");
  EXPECT_EQ(back.deadline_ms(), 2500u);
  ASSERT_EQ(back.waypoints().size(), 3u);
  EXPECT_EQ(back.waypoints()[2].lat_deg(), 49.0);
  EXPECT_EQ(back.waypoints()[2].alt_rel_home_m(), 22.0f);
  EXPECT_TRUE(std::isnan(back.waypoints()[1].hold_s()));
  EXPECT_TRUE(std::isnan(back.params().heading_deg()));
}

TEST(Json, CommandParamsNullIsNaN) {
  auto j = to_json_value(skyrelay::v1::CommandParams{});
  for (auto& [k, v] : j.items()) v = nullptr;
  j["alt_rel_home_m"] = 10;
  skyrelay::v1::CommandParams p;
  skyrelay::v1::from_json_value(j, p);
  EXPECT_EQ(p.alt_rel_home_m(), 10.0f);
  EXPECT_TRUE(std::isnan(p.lat_deg()));
  EXPECT_TRUE(std::isnan(p.zoom_level()));
}

TEST(Json, FromJsonRejectsBadCommandId) {
  skyrelay::v1::CommandStatus s;
  auto j = to_json_value(s);
  for (const char* bad : {"18446744073709551616", "123456789012345678901", "+1", "-1", "", "1.0", " 1", "1a"}) {
    j["command_id"] = bad;
    skyrelay::v1::CommandStatus out;
    EXPECT_THROW(skyrelay::v1::from_json_value(j, out), JsonError) << bad;
  }
  j["command_id"] = 5;
  skyrelay::v1::CommandStatus out;
  EXPECT_THROW(skyrelay::v1::from_json_value(j, out), JsonError);
}

TEST(JsonDetail, GetId) {
  using skyrelay::v1::json_detail::get_id;
  const auto id = [](const char* s) { return get_id(nlohmann::json(s), "id"); };
  EXPECT_EQ(id("0"), 0u);
  EXPECT_EQ(id("00000000000000000042"), 42u);
  EXPECT_EQ(id("007"), 7u);
  EXPECT_EQ(id("18446744073709551615"), 18446744073709551615ull);
  const auto msg = [&](const char* s) -> std::string {
    try {
      id(s);
    } catch (const JsonError& e) {
      return e.what();
    }
    return "accepted";
  };
  EXPECT_EQ(msg("18446744073709551616"), "id: out of range");
  EXPECT_EQ(msg("99999999999999999999"), "id: out of range");
  EXPECT_EQ(msg("123456789012345678901"), "id: expected a decimal string");
  EXPECT_EQ(msg("+1"), "id: expected a decimal string");
  EXPECT_EQ(msg("-1"), "id: expected a decimal string");
  EXPECT_EQ(msg(""), "id: expected a decimal string");
  EXPECT_EQ(msg("1.0"), "id: expected a decimal string");
  EXPECT_THROW(get_id(nlohmann::json(1), "id"), JsonError);
}

TEST(JsonDetail, GetFloat) {
  using skyrelay::v1::json_detail::get_float;
  EXPECT_TRUE(std::isnan(get_float<float>(nlohmann::json(nullptr), "f")));
  EXPECT_TRUE(std::isnan(get_float<double>(nlohmann::json(nullptr), "f")));
  EXPECT_EQ(get_float<float>(nlohmann::json(2.5), "f"), 2.5f);
  EXPECT_EQ(get_float<double>(nlohmann::json(1e300), "f"), 1e300);
  EXPECT_EQ(get_float<float>(nlohmann::json(7), "f"), 7.0f);
  EXPECT_EQ(get_float<float>(nlohmann::json(static_cast<double>(std::numeric_limits<float>::max())), "f"),
            std::numeric_limits<float>::max());
  EXPECT_THROW(get_float<float>(nlohmann::json("1"), "f"), JsonError);
  EXPECT_THROW(get_float<float>(nlohmann::json(true), "f"), JsonError);
  for (double big : {1e39, -1e39, 3.5e38, 1e300}) {
    try {
      get_float<float>(nlohmann::json(big), "params.zoom_level");
      ADD_FAILURE() << big;
    } catch (const JsonError& e) {
      EXPECT_STREQ(e.what(), "params.zoom_level: out of range");
    }
  }
}

TEST(ValidBits, CommandStatusCore) {
  using S = skyrelay::v1::valid::CommandStatus;
  EXPECT_EQ(S::kCore, S::kind | S::state | S::t_received_ns);
}

TEST(ValidBits, CommandRequestCore) {
  using S = skyrelay::v1::valid::CommandRequest;
  EXPECT_EQ(S::kCore, S::kind | S::params | S::waypoints | S::deadline_ms | S::confirm_token |
                          S::operator_name);
}
