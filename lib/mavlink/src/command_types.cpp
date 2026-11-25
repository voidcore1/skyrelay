#include "skyrelay/vehicle/command_types.hpp"

#include <utility>

namespace skyrelay::vehicle {

bool is_terminal(CommandState s) {
  switch (s) {
    case CommandState::Accepted:
    case CommandState::Denied:
    case CommandState::Failed:
    case CommandState::TimedOut:
    case CommandState::Cancelled:
    case CommandState::RejectedInvalid:
    case CommandState::RejectedUnsupported:
    case CommandState::RejectedBusy:
    case CommandState::RejectedUnauthorised:
      return true;
    case CommandState::Unknown:
    case CommandState::Received:
    case CommandState::Sent:
    case CommandState::InProgress:
      return false;
  }
  return false;
}

std::string_view to_string(CommandKind k) {
  switch (k) {
    case CommandKind::Unknown: return "UNKNOWN";
    case CommandKind::Arm: return "ARM";
    case CommandKind::Disarm: return "DISARM";
    case CommandKind::Takeoff: return "TAKEOFF";
    case CommandKind::Land: return "LAND";
    case CommandKind::ReturnHome: return "RETURN_HOME";
    case CommandKind::Hold: return "HOLD";
    case CommandKind::Goto: return "GOTO";
    case CommandKind::SetSpeed: return "SET_SPEED";
    case CommandKind::SetAltitude: return "SET_ALTITUDE";
    case CommandKind::SetHeading: return "SET_HEADING";
    case CommandKind::MissionUpload: return "MISSION_UPLOAD";
    case CommandKind::MissionStart: return "MISSION_START";
    case CommandKind::MissionPause: return "MISSION_PAUSE";
    case CommandKind::MissionClear: return "MISSION_CLEAR";
    case CommandKind::GimbalPoint: return "GIMBAL_POINT";
    case CommandKind::CameraZoom: return "CAMERA_ZOOM";
    case CommandKind::CameraCapture: return "CAMERA_CAPTURE";
    case CommandKind::VideoStart: return "VIDEO_START";
    case CommandKind::VideoStop: return "VIDEO_STOP";
    case CommandKind::EmergencyAbort: return "EMERGENCY_ABORT";
    case CommandKind::EmergencyHover: return "EMERGENCY_HOVER";
    case CommandKind::EmergencyKill: return "EMERGENCY_KILL";
  }
  return "UNKNOWN";
}

CommandParams make_params() { return CommandParams{}; }

void reject(const CommandRequestData& req, const Rejection& why, uint64_t rt_ns,
            Outbox& out) {
  CommandStatusData s;
  s.key = req.key;
  s.kind = req.kind;
  s.state = CommandState::Received;
  s.t_received_ns = rt_ns;
  out.statuses.push_back(s);

  s.state = why.state;
  s.detail = why.detail;
  s.t_terminal_ns = rt_ns;
  out.statuses.push_back(std::move(s));
}

}  // namespace skyrelay::vehicle
