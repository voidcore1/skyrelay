#include <gtest/gtest.h>

#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include "skyrelay/vehicle/command_client.hpp"
#include "skyrelay/vehicle/command_types.hpp"
#include "skyrelay/vehicle/command_validation.hpp"

using namespace skyrelay::vehicle;

namespace {

constexpr uint32_t kVehicle = 2;
constexpr uint64_t kCommandId = 0x1234'5678'9abc'def0;

CommandRequestData request(CommandKind kind) {
  CommandRequestData r;
  r.key = {kVehicle, 1, kCommandId};
  r.kind = kind;
  return r;
}

Waypoint waypoint(double lat = 47.3977, double lon = 8.5456, float alt = 20) {
  Waypoint w;
  w.lat_deg = lat;
  w.lon_deg = lon;
  w.alt_rel_home_m = alt;
  return w;
}

CommandRequestData takeoff(float alt) {
  auto r = request(CommandKind::Takeoff);
  r.params.alt_rel_home_m = alt;
  return r;
}

CommandRequestData go_to() {
  auto r = request(CommandKind::Goto);
  r.params.lat_deg = 47.3981;
  r.params.lon_deg = 8.5461;
  r.params.alt_rel_home_m = 15;
  return r;
}

CommandRequestData upload(std::size_t n) {
  auto r = request(CommandKind::MissionUpload);
  r.waypoints.assign(n, waypoint());
  return r;
}

CommandRequestData kill() {
  auto r = request(CommandKind::EmergencyKill);
  r.confirm_token = kCommandId ^ kVehicle;
  return r;
}

std::string read_file(const std::string& path) {
  std::ifstream f(path);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

struct Row {
  const char* name;
  CommandRequestData req;
  CommandOrigin origin;
  std::optional<CommandState> state;  // nullopt: valid
  std::string detail;
};

template <class F>
CommandRequestData with(CommandRequestData r, F f) {
  f(r);
  return r;
}

constexpr auto kNormal = CommandOrigin::Normal;
constexpr auto kKill = CommandOrigin::Kill;
constexpr auto kInvalid = CommandState::RejectedInvalid;

}  // namespace

TEST(CommandValidation, Table) {
  const float nan = kNaNf;
  const std::vector<Row> rows = {
      {"plain ARM", request(CommandKind::Arm), kNormal, {}, ""},
      {"issuer 0", with(request(CommandKind::Arm), [](auto& r) { r.key.issuer_id = 0; }),
       kNormal, kInvalid, "issuer_id and command_id must be non-zero"},
      {"command 0", with(request(CommandKind::Arm), [](auto& r) { r.key.command_id = 0; }),
       kNormal, kInvalid, "issuer_id and command_id must be non-zero"},
      {"ids before kind",
       with(request(CommandKind::Arm), [](auto& r) { r.key.issuer_id = 0; r.unknown_kind = 99; }),
       kNormal, kInvalid, "issuer_id and command_id must be non-zero"},
      {"unknown kind value",
       with(request(CommandKind::Unknown), [](auto& r) { r.unknown_kind = 99; }), kNormal,
       CommandState::RejectedUnsupported, "unknown command kind 99"},
      {"kind UNKNOWN", request(CommandKind::Unknown), kNormal, kInvalid, "kind is UNKNOWN"},
      {"kill on normal partition", kill(), kNormal, CommandState::RejectedUnauthorised,
       "EMERGENCY_KILL is only accepted on the kill partition"},
      {"ARM on kill partition", request(CommandKind::Arm), kKill, kInvalid,
       "only EMERGENCY_KILL is accepted on the kill partition"},
      {"kill with wrong token", with(kill(), [](auto& r) { r.confirm_token ^= 1; }), kKill,
       kInvalid, "confirm_token does not match"},
      {"kill without token", with(kill(), [](auto& r) { r.confirm_token = 0; }), kKill,
       kInvalid, "confirm_token does not match"},
      {"kill token before deadline",
       with(kill(), [](auto& r) { r.confirm_token = 0; r.deadline_ms = 5; }), kKill, kInvalid,
       "confirm_token does not match"},
      {"kill", kill(), kKill, {}, ""},
      {"deadline 0 is the default", with(request(CommandKind::Arm), [](auto& r) { r.deadline_ms = 0; }),
       kNormal, {}, ""},
      {"deadline 99", with(request(CommandKind::Arm), [](auto& r) { r.deadline_ms = 99; }),
       kNormal, kInvalid, "deadline_ms out of range: 99 (allowed 100-60000)"},
      {"deadline 100", with(request(CommandKind::Arm), [](auto& r) { r.deadline_ms = 100; }),
       kNormal, {}, ""},
      {"deadline 60000", with(request(CommandKind::Arm), [](auto& r) { r.deadline_ms = 60000; }),
       kNormal, {}, ""},
      {"deadline 60001", with(request(CommandKind::Arm), [](auto& r) { r.deadline_ms = 60001; }),
       kNormal, kInvalid, "deadline_ms out of range: 60001 (allowed 100-60000)"},
      {"waypoints on GOTO", with(go_to(), [](auto& r) { r.waypoints = {waypoint()}; }), kNormal,
       kInvalid, "waypoints are only used by MISSION_UPLOAD"},
      {"deadline before waypoints",
       with(go_to(), [](auto& r) { r.waypoints = {waypoint()}; r.deadline_ms = 1; }), kNormal,
       kInvalid, "deadline_ms out of range: 1 (allowed 100-60000)"},
      {"LAND reads nothing", with(request(CommandKind::Land), [](auto& r) { r.params.alt_rel_home_m = -5; }),
       kNormal, {}, ""},

      {"TAKEOFF 1 m", takeoff(1), kNormal, {}, ""},
      {"TAKEOFF 500 m", takeoff(500), kNormal, {}, ""},
      {"TAKEOFF 0.5 m", takeoff(0.5f), kNormal, kInvalid,
       "alt_rel_home_m out of range: 0.5 (allowed 1-500)"},
      {"TAKEOFF 500.5 m", takeoff(500.5f), kNormal, kInvalid,
       "alt_rel_home_m out of range: 500.5 (allowed 1-500)"},
      {"TAKEOFF NaN", takeoff(nan), kNormal, kInvalid,
       "alt_rel_home_m out of range: nan (allowed 1-500)"},
      {"SET_ALTITUDE 501 m",
       with(request(CommandKind::SetAltitude), [](auto& r) { r.params.alt_rel_home_m = 501; }),
       kNormal, kInvalid, "alt_rel_home_m out of range: 501 (allowed 1-500)"},
      {"SET_ALTITUDE 120 m",
       with(request(CommandKind::SetAltitude), [](auto& r) { r.params.alt_rel_home_m = 120; }),
       kNormal, {}, ""},

      {"GOTO", go_to(), kNormal, {}, ""},
      {"GOTO at the edges", with(go_to(), [](auto& r) {
         r.params.lat_deg = -90;
         r.params.lon_deg = 180;
         r.params.ground_speed_mps = 30;
         r.params.heading_deg = 0;
       }), kNormal, {}, ""},
      {"GOTO lat 90.5", with(go_to(), [](auto& r) { r.params.lat_deg = 90.5; }), kNormal,
       kInvalid, "lat_deg out of range: 90.5 (allowed -90-90)"},
      {"GOTO lat NaN", with(go_to(), [](auto& r) { r.params.lat_deg = kNaN; }), kNormal,
       kInvalid, "lat_deg out of range: nan (allowed -90-90)"},
      {"GOTO lon -180.5", with(go_to(), [](auto& r) { r.params.lon_deg = -180.5; }), kNormal,
       kInvalid, "lon_deg out of range: -180.5 (allowed -180-180)"},
      {"GOTO alt 0", with(go_to(), [](auto& r) { r.params.alt_rel_home_m = 0; }), kNormal,
       kInvalid, "alt_rel_home_m out of range: 0 (allowed 1-500)"},
      {"GOTO speed 0", with(go_to(), [](auto& r) { r.params.ground_speed_mps = 0; }), kNormal,
       kInvalid, "ground_speed_mps out of range: 0 (allowed 0-30)"},
      {"GOTO speed 30.5", with(go_to(), [](auto& r) { r.params.ground_speed_mps = 30.5f; }),
       kNormal, kInvalid, "ground_speed_mps out of range: 30.5 (allowed 0-30)"},
      {"GOTO heading 360", with(go_to(), [](auto& r) { r.params.heading_deg = 360; }), kNormal,
       kInvalid, "heading_deg out of range: 360 (allowed 0-360)"},
      {"GOTO heading -1", with(go_to(), [](auto& r) { r.params.heading_deg = -1; }), kNormal,
       kInvalid, "heading_deg out of range: -1 (allowed 0-360)"},

      {"SET_SPEED 30",
       with(request(CommandKind::SetSpeed), [](auto& r) { r.params.ground_speed_mps = 30; }),
       kNormal, {}, ""},
      {"SET_SPEED 0",
       with(request(CommandKind::SetSpeed), [](auto& r) { r.params.ground_speed_mps = 0; }),
       kNormal, kInvalid, "ground_speed_mps out of range: 0 (allowed 0-30)"},
      {"SET_SPEED NaN", request(CommandKind::SetSpeed), kNormal, kInvalid,
       "ground_speed_mps out of range: nan (allowed 0-30)"},
      {"SET_HEADING 359.5",
       with(request(CommandKind::SetHeading), [](auto& r) { r.params.heading_deg = 359.5f; }),
       kNormal, {}, ""},
      {"SET_HEADING 360",
       with(request(CommandKind::SetHeading), [](auto& r) { r.params.heading_deg = 360; }),
       kNormal, kInvalid, "heading_deg out of range: 360 (allowed 0-360)"},
      {"SET_HEADING NaN", request(CommandKind::SetHeading), kNormal, kInvalid,
       "heading_deg out of range: nan (allowed 0-360)"},

      {"upload 1", upload(1), kNormal, {}, ""},
      {"upload 500", upload(500), kNormal, {}, ""},
      {"upload 0", upload(0), kNormal, kInvalid, "waypoints out of range: 0 (allowed 1-500)"},
      {"upload 501", upload(501), kNormal, kInvalid,
       "waypoints out of range: 501 (allowed 1-500)"},
      {"upload with hold and radius", with(upload(2), [](auto& r) {
         r.waypoints[0].hold_s = 0;
         r.waypoints[0].acceptance_radius_m = 1000;
         r.waypoints[1].hold_s = 3600;
       }), kNormal, {}, ""},
      {"waypoint lat NaN", with(upload(3), [](auto& r) { r.waypoints[1].lat_deg = kNaN; }),
       kNormal, kInvalid, "waypoints[1].lat_deg out of range: nan (allowed -90-90)"},
      {"waypoint lon 181", with(upload(3), [](auto& r) { r.waypoints[2].lon_deg = 181; }),
       kNormal, kInvalid, "waypoints[2].lon_deg out of range: 181 (allowed -180-180)"},
      {"waypoint alt 0", with(upload(3), [](auto& r) { r.waypoints[0].alt_rel_home_m = 0; }),
       kNormal, kInvalid, "waypoints[0].alt_rel_home_m out of range: 0 (allowed 1-500)"},
      {"waypoint hold -1", with(upload(3), [](auto& r) { r.waypoints[2].hold_s = -1; }),
       kNormal, kInvalid, "waypoints[2].hold_s out of range: -1 (allowed 0-3600)"},
      {"waypoint hold 3601", with(upload(3), [](auto& r) { r.waypoints[2].hold_s = 3601; }),
       kNormal, kInvalid, "waypoints[2].hold_s out of range: 3601 (allowed 0-3600)"},
      {"waypoint radius 0",
       with(upload(3), [](auto& r) { r.waypoints[0].acceptance_radius_m = 0; }), kNormal,
       kInvalid, "waypoints[0].acceptance_radius_m out of range: 0 (allowed 0-1000)"},
      {"waypoint radius 1001",
       with(upload(3), [](auto& r) { r.waypoints[1].acceptance_radius_m = 1001; }), kNormal,
       kInvalid, "waypoints[1].acceptance_radius_m out of range: 1001 (allowed 0-1000)"},
  };

  for (const auto& row : rows) {
    SCOPED_TRACE(row.name);
    auto rej = validate_request(row.req, row.origin);
    if (!row.state) {
      EXPECT_FALSE(rej) << rej->detail;
      continue;
    }
    ASSERT_TRUE(rej);
    EXPECT_EQ(rej->state, *row.state);
    EXPECT_EQ(rej->detail, row.detail);
  }
}

TEST(CommandValidation, LimitsCanBeTightened) {
  CommandLimits limits;
  limits.max_alt_m = 120;
  auto rej = validate_request(takeoff(121), kNormal, limits);
  ASSERT_TRUE(rej);
  EXPECT_EQ(rej->detail, "alt_rel_home_m out of range: 121 (allowed 1-120)");
  EXPECT_FALSE(validate_request(takeoff(120), kNormal, limits));
}

// The numbers here are part of the contract. Changing one means changing
// docs/contract.md in the same commit; ContractCitesTheLimits checks that.
TEST(CommandValidation, LimitsArePinned) {
  EXPECT_EQ(kMinAltitudeM, 1.0f);
  EXPECT_EQ(kMaxAltitudeM, 500.0f);
  EXPECT_EQ(kMaxGroundSpeedMps, 30.0f);
  EXPECT_EQ(kHeadingEndDeg, 360.0f);
  EXPECT_EQ(kMaxLatDeg, 90.0);
  EXPECT_EQ(kMaxLonDeg, 180.0);
  EXPECT_EQ(kMinDeadlineMs, 100u);
  EXPECT_EQ(kMaxDeadlineMs, 60000u);
  EXPECT_EQ(kMaxWaypoints, 500u);
  EXPECT_EQ(kMaxHoldS, 3600.0f);
  EXPECT_EQ(kMaxAcceptanceRadiusM, 1000.0f);

  const CommandLimits d;
  EXPECT_EQ(d.min_alt_m, kMinAltitudeM);
  EXPECT_EQ(d.max_alt_m, kMaxAltitudeM);
  EXPECT_EQ(d.max_speed_mps, kMaxGroundSpeedMps);
  EXPECT_EQ(d.min_deadline_ms, kMinDeadlineMs);
  EXPECT_EQ(d.max_deadline_ms, kMaxDeadlineMs);
  EXPECT_EQ(d.max_waypoints, kMaxWaypoints);
  EXPECT_EQ(d.max_hold_s, kMaxHoldS);
  EXPECT_EQ(d.max_acceptance_radius_m, kMaxAcceptanceRadiusM);

  const CommandTiming t;
  EXPECT_EQ(t.default_deadline.count(), 1000);
  EXPECT_EQ(t.in_progress_max.count(), 10000);
  EXPECT_EQ(t.duplicate_ack_quarantine.count(), 700);
}

TEST(CommandValidation, ContractCitesTheLimits) {
  const std::string doc = read_file(SKYRELAY_SOURCE_DIR "/docs/contract.md");
  ASSERT_FALSE(doc.empty());
  const auto default_ms = CommandTiming{}.default_deadline.count();
  const std::vector<std::string> expected = {
      std::format("`deadline_ms` is 0 for the default ({} ms) or {} to {}.", default_ms,
                  kMinDeadlineMs, kMaxDeadlineMs),
      std::format("`waypoints` holds at most {} items", kMaxWaypoints),
      std::format("4. `deadline_ms` is 0 (default, {} ms) or {} to {}.", default_ms,
                  kMinDeadlineMs, kMaxDeadlineMs),
      std::format("| `TAKEOFF`, `SET_ALTITUDE` | `alt_rel_home_m` | {} to {} |", kMinAltitudeM,
                  kMaxAltitudeM),
      std::format("| `GOTO` | `lat_deg` | -{} to {} |", kMaxLatDeg, kMaxLatDeg),
      std::format("| | `lon_deg` | -{} to {} |", kMaxLonDeg, kMaxLonDeg),
      std::format("| | `alt_rel_home_m` | {} to {} |", kMinAltitudeM, kMaxAltitudeM),
      std::format("| | `ground_speed_mps` | NaN, or above 0 up to {} |", kMaxGroundSpeedMps),
      std::format("| | `heading_deg` | NaN, or 0 up to (not including) {} |", kHeadingEndDeg),
      std::format("| `SET_SPEED` | `ground_speed_mps` | above 0 up to {} |", kMaxGroundSpeedMps),
      std::format("| `SET_HEADING` | `heading_deg` | 0 up to (not including) {} |",
                  kHeadingEndDeg),
      std::format("| `MISSION_UPLOAD` | `waypoints` | 1 to {} items |", kMaxWaypoints),
      std::format("| | waypoint `alt_rel_home_m` | {} to {} |", kMinAltitudeM, kMaxAltitudeM),
      std::format("| | waypoint `hold_s` | NaN, or 0 to {} |", kMaxHoldS),
      std::format("| | waypoint `acceptance_radius_m` | NaN, or above 0 up to {} |",
                  kMaxAcceptanceRadiusM),
  };
  for (const auto& line : expected) {
    EXPECT_NE(doc.find(line), std::string::npos) << "docs/contract.md lacks: " << line;
  }
}

TEST(CommandTypes, MakeParamsLeavesEveryFieldUnset) {
  const CommandParams p = make_params();
  EXPECT_TRUE(std::isnan(p.lat_deg));
  EXPECT_TRUE(std::isnan(p.lon_deg));
  EXPECT_TRUE(std::isnan(p.alt_rel_home_m));
  EXPECT_TRUE(std::isnan(p.ground_speed_mps));
  EXPECT_TRUE(std::isnan(p.heading_deg));
  EXPECT_TRUE(std::isnan(p.gimbal_pitch_deg));
  EXPECT_TRUE(std::isnan(p.gimbal_yaw_deg));
  EXPECT_TRUE(std::isnan(p.zoom_level));
  // A request built from scratch starts the same way.
  EXPECT_TRUE(std::isnan(CommandRequestData{}.params.alt_rel_home_m));
  EXPECT_TRUE(std::isnan(Waypoint{}.hold_s));
}

TEST(CommandTypes, TerminalStates) {
  const CommandState terminal[] = {
      CommandState::Accepted,        CommandState::Denied,
      CommandState::Failed,          CommandState::TimedOut,
      CommandState::Cancelled,       CommandState::RejectedInvalid,
      CommandState::RejectedUnsupported, CommandState::RejectedBusy,
      CommandState::RejectedUnauthorised};
  for (auto s : terminal) EXPECT_TRUE(is_terminal(s)) << int(s);
  for (auto s : {CommandState::Unknown, CommandState::Received, CommandState::Sent,
                 CommandState::InProgress}) {
    EXPECT_FALSE(is_terminal(s)) << int(s);
  }
}

// The enum mirrors the IDL value for value, and to_string gives the IDL
// literal without its prefix.
TEST(CommandTypes, KindNamesMirrorTheIdl) {
  const std::string idl = read_file(SKYRELAY_SOURCE_DIR "/idl/skyrelay/v1/types.idl");
  const auto begin = idl.find("enum CommandKind {");
  ASSERT_NE(begin, std::string::npos);
  const auto end = idl.find("};", begin);
  const std::string body = idl.substr(begin, end - begin);
  const std::regex literal("COMMAND_KIND_([A-Z_]+)");
  std::vector<std::string> names;
  for (auto it = std::sregex_iterator(body.begin(), body.end(), literal);
       it != std::sregex_iterator(); ++it) {
    names.push_back((*it)[1]);
  }
  ASSERT_EQ(names.size(), 23u);
  for (std::size_t i = 0; i < names.size(); ++i) {
    EXPECT_EQ(to_string(static_cast<CommandKind>(i)), names[i]) << i;
  }
  EXPECT_EQ(names.size() - 1, static_cast<std::size_t>(CommandKind::EmergencyKill));
}

// Every status carries t_received_ns, and every terminal one, rejections
// included, carries t_terminal_ns.
TEST(CommandTypes, RejectPublishesReceivedThenTheRejection) {
  Outbox out;
  auto req = request(CommandKind::Takeoff);
  reject(req, {CommandState::RejectedBusy, "link LOST"}, 5000, out);
  EXPECT_TRUE(out.commands.empty());
  ASSERT_EQ(out.statuses.size(), 2u);

  const auto& received = out.statuses[0];
  EXPECT_EQ(received.key, req.key);
  EXPECT_EQ(received.kind, CommandKind::Takeoff);
  EXPECT_EQ(received.state, CommandState::Received);
  EXPECT_EQ(received.t_received_ns, std::optional<uint64_t>{5000});
  EXPECT_FALSE(received.t_terminal_ns);
  EXPECT_TRUE(received.detail.empty());

  const auto& rejected = out.statuses[1];
  EXPECT_EQ(rejected.key, req.key);
  EXPECT_EQ(rejected.kind, CommandKind::Takeoff);
  EXPECT_EQ(rejected.state, CommandState::RejectedBusy);
  EXPECT_EQ(rejected.detail, "link LOST");
  EXPECT_EQ(rejected.t_received_ns, std::optional<uint64_t>{5000});
  EXPECT_EQ(rejected.t_terminal_ns, std::optional<uint64_t>{5000});
  EXPECT_FALSE(rejected.t_sent_ns);
  EXPECT_FALSE(rejected.t_in_progress_ns);
  EXPECT_FALSE(rejected.mav_result);
  EXPECT_TRUE(std::isnan(rejected.progress_pct));
  EXPECT_EQ(rejected.attempts, 0);
}
