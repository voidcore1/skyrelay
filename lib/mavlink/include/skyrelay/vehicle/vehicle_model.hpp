#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "skyrelay/mav/mavlink.hpp"
#include "skyrelay/vehicle/adapter.hpp"
#include "skyrelay/vehicle/types.hpp"

namespace skyrelay::vehicle {

// Which parts of the model one apply() touched. Bit flags; append only.
// Reboot: time_boot_ms went back by more than kRebootBackstepMs, so the
// vehicle restarted. The SYSTEM_TIME offset is already dropped by then.
enum class Changed : uint8_t {
  None = 0,
  Pose = 1,
  State = 2,
  Health = 4,
  Info = 8,
  Time = 16,
  Reboot = 32
};

constexpr Changed operator|(Changed a, Changed b) {
  return static_cast<Changed>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr Changed operator&(Changed a, Changed b) {
  return static_cast<Changed>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
constexpr Changed& operator|=(Changed& a, Changed b) { return a = a | b; }
// True when flag is set in value.
constexpr bool has(Changed value, Changed flag) {
  return (value & flag) != Changed::None;
}

// Folds raw MAVLink messages from one vehicle into normalised state. Not
// thread-safe; the owner serialises access.
//
// The autopilot adapter is chosen from the first HEARTBEAT that comes from
// component 1 (MAV_COMP_ID_AUTOPILOT1) and is not a GCS. Heartbeats from other
// components or from a GCS are ignored, and once the vehicle is known so is
// any message from a different system id.
class VehicleModel {
 public:
  // A time_boot_ms this far below the highest one seen from the autopilot
  // (SYSTEM_TIME, GLOBAL_POSITION_INT, ATTITUDE_QUATERNION) means a reboot.
  // Anything smaller is taken as reordering. A uint32 ms wrap after 49 days of
  // uptime also reads as a reboot, which only costs one extra setup.
  static constexpr uint32_t kRebootBackstepMs = 1000;

  // Returns what changed. Messages the model does not use give None. A flag is
  // set whenever the message was one that feeds that part, even if the values
  // came out the same, so callers can use it to pace periodic publishing.
  Changed apply(const mavlink_message_t& msg);

  const PoseData& pose() const { return pose_; }
  const StateData& state() const { return state_; }
  const HealthData& health() const { return health_; }
  const InfoData& info() const { return info_; }
  // The adapter chosen at the lock; null until the first autopilot HEARTBEAT.
  const AutopilotAdapter* adapter() const { return adapter_.get(); }

  // Vehicle UTC in microseconds for a time_boot_ms stamp, using the offset
  // from the last SYSTEM_TIME. 0 until one with a non-zero unix time arrives.
  uint64_t source_time_us(uint32_t time_boot_ms) const;

  // Whether the most recent apply() changed a discrete field, meaning a value
  // a consumer should hear about at once rather than on the next periodic
  // publish. A field going from unknown to known counts as a change. Each
  // apply() starts from false, so a message that touches neither reports false.
  // State: armed, mode (including its raw text), landed, system_status.
  // Health: gnss_fix, gnss_satellites, the three sensors_* masks, estimator_ok.
  // Continuous values such as voltage or hdop never set it.
  bool state_changed_discrete() const { return state_discrete_; }
  bool health_changed_discrete() const { return health_discrete_; }

  // Forget what is learned from AUTOPILOT_VERSION (firmware, capabilities),
  // the SYSTEM_TIME offset and the highest time_boot_ms, so a reconnect
  // re-learns them and its first time stamp cannot read as a reboot. The
  // adapter and everything else stays.
  void reset_for_resetup();

 private:
  Changed on_heartbeat(const mavlink_message_t& msg);
  Changed on_global_position(const mavlink_message_t& msg);
  Changed on_attitude(const mavlink_message_t& msg);
  Changed on_sys_status(const mavlink_message_t& msg);
  Changed on_battery_status(const mavlink_message_t& msg);
  Changed on_gps_raw(const mavlink_message_t& msg);
  Changed on_extended_sys_state(const mavlink_message_t& msg);
  Changed on_system_time(const mavlink_message_t& msg);
  Changed on_autopilot_version(const mavlink_message_t& msg);
  // Records a time_boot_ms; on a reboot drops the time offset and returns
  // Changed::Reboot, otherwise Changed::None.
  Changed note_boot_time(uint32_t time_boot_ms);

  std::unique_ptr<AutopilotAdapter> adapter_;
  PoseData pose_;
  StateData state_;
  HealthData health_;
  InfoData info_;
  bool have_time_offset_{false};
  int64_t time_offset_us_{0};
  std::optional<uint32_t> max_boot_ms_;
  bool state_discrete_{false};
  bool health_discrete_{false};
};

}  // namespace skyrelay::vehicle
