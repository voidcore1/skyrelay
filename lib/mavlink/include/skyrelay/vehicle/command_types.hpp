#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "skyrelay/mav/command_transaction.hpp"
#include "skyrelay/vehicle/types.hpp"

namespace skyrelay::vehicle {

// Commands as the bridge sees them, free of DDS types like the rest of the
// vehicle model. CommandKind and CommandState mirror the IDL enums value for
// value, with Unknown first. Only append.

enum class CommandKind : uint8_t {
  Unknown,
  Arm,
  Disarm,
  Takeoff,
  Land,
  ReturnHome,
  Hold,
  Goto,
  SetSpeed,
  SetAltitude,
  SetHeading,
  MissionUpload,
  MissionStart,
  MissionPause,
  MissionClear,
  GimbalPoint,
  CameraZoom,
  CameraCapture,
  VideoStart,
  VideoStop,
  EmergencyAbort,
  EmergencyHover,
  EmergencyKill
};

enum class CommandState : uint8_t {
  Unknown,
  Received,
  Sent,
  InProgress,
  Accepted,
  Denied,
  Failed,
  TimedOut,
  Cancelled,
  RejectedInvalid,
  RejectedUnsupported,
  RejectedBusy,
  RejectedUnauthorised
};

// True for every REJECTED_*, ACCEPTED, DENIED, FAILED, TIMED_OUT and
// CANCELLED: the states a command never leaves.
bool is_terminal(CommandState s);

// The IDL literal without its COMMAND_KIND_ prefix ("TAKEOFF"), for details
// and logs.
std::string_view to_string(CommandKind k);

// The command partition a request arrived on: <fleet>.cmd or <fleet>.cmd.kill.
enum class CommandOrigin : uint8_t { Normal, Kill };

struct CommandKey {
  uint32_t vehicle_id = 0;
  uint32_t issuer_id = 0;
  uint64_t command_id = 0;

  auto operator<=>(const CommandKey&) const = default;
};

// NaN hold_s means no hold; NaN acceptance_radius_m, the autopilot's default.
struct Waypoint {
  double lat_deg = kNaN;
  double lon_deg = kNaN;
  float alt_rel_home_m = kNaNf;
  float hold_s = kNaNf;
  float acceptance_radius_m = kNaNf;
};

// One field per parameter any kind reads (docs/contract.md says which kind
// reads what). A field the kind does not read is NaN, and so is "keep the
// current value"; 0 is always a value.
struct CommandParams {
  double lat_deg = kNaN;
  double lon_deg = kNaN;
  float alt_rel_home_m = kNaNf;
  float ground_speed_mps = kNaNf;
  float heading_deg = kNaNf;
  float gimbal_pitch_deg = kNaNf;
  float gimbal_yaw_deg = kNaNf;
  float zoom_level = kNaNf;
};

// Every field NaN, which is "not used" for every kind. Issuers start from
// this and set only the fields their kind reads, so a field they forget goes
// out as NaN rather than as 0, which a vehicle would take as a real value.
CommandParams make_params();

struct CommandRequestData {
  CommandKey key;
  CommandKind kind = CommandKind::Unknown;
  // Set when the request carried an IDL kind value this build does not know;
  // kind is then Unknown.
  std::optional<uint32_t> unknown_kind;
  CommandParams params;
  std::vector<Waypoint> waypoints;
  uint32_t deadline_ms = 0;  // 0: the default
  uint64_t confirm_token = 0;
  std::string operator_name;
};

// One command_status sample. Every status has t_received_ns; t_sent_ns is set
// from SENT on, t_in_progress_ns from the first IN_PROGRESS on, and
// t_terminal_ns only on a terminal status. mav_result is the last
// COMMAND_ACK.result of the MAVLink command in flight, attempts the MAVLink
// sends of the whole request, retries included.
struct CommandStatusData {
  CommandKey key;
  CommandKind kind = CommandKind::Unknown;
  CommandState state = CommandState::Unknown;
  std::optional<uint8_t> mav_result;
  std::optional<uint8_t> mission_result;
  float progress_pct = kNaNf;
  std::string detail;
  std::optional<uint64_t> t_received_ns;
  std::optional<uint64_t> t_sent_ns;
  std::optional<uint64_t> t_in_progress_ns;
  std::optional<uint64_t> t_terminal_ns;
  uint8_t attempts = 0;
};

// Why a request will not be sent: one of the REJECTED_* states and a reason.
struct Rejection {
  CommandState state;
  std::string detail;
};

enum class MissionOp : uint8_t { None, Upload, Clear };

// What a request turns into on the wire. The steps go out one at a time, each
// only after the one before it was ACCEPTED. If the last step ends DENIED or
// FAILED and there is a fallback, the fallback goes out next. A mission
// request has no steps and names the mission operation instead.
struct CommandPlan {
  std::vector<mav::CommandSpec> steps;
  std::optional<mav::CommandSpec> fallback;
  MissionOp mission = MissionOp::None;
};

using PlanResult = std::variant<CommandPlan, Rejection>;

// What one call into a command or mission client produced, in order: the
// MAVLink messages to send and the statuses to publish.
struct Outbox {
  std::vector<mav::SendRequest> commands;
  std::vector<CommandStatusData> statuses;
};

// Publishes RECEIVED and then the rejection's REJECTED_* status, both stamped
// rt_ns. The rejection is terminal, so it carries t_terminal_ns as well.
void reject(const CommandRequestData& req, const Rejection& why, uint64_t rt_ns,
            Outbox& out);

}  // namespace skyrelay::vehicle
