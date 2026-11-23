#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

#include "skyrelay/common/clock.hpp"
#include "skyrelay/common/rate_limiter.hpp"
#include "skyrelay/mav/framer.hpp"
#include "skyrelay/mav/link_monitor.hpp"
#include "skyrelay/mav/packer.hpp"
#include "skyrelay/mav/setup_sequence.hpp"
#include "skyrelay/vehicle/telemetry_sink.hpp"
#include "skyrelay/vehicle/vehicle_model.hpp"

namespace skyrelay::vehicle {

// Publish rates in Hz. pose is a cap on an event stream; the others are the
// periodic rates, on top of which state and health also go out on change.
struct Rates {
  double pose_hz = 20;
  double state_hz = 5;
  double health_hz = 2;
  double link_hz = 1;
};

struct SessionConfig {
  uint32_t vehicle_id;
  // Lock to this system id only. Unset: lock to the first autopilot HEARTBEAT.
  std::optional<uint8_t> expected_sysid;
  uint8_t gcs_sysid = 245;
  mav::LinkThresholds link{};
  Rates rates{};
};

// Where the session's outgoing bytes go: one MAVLink frame per call.
class Transport {
 public:
  virtual ~Transport() = default;
  virtual void send(std::span<const uint8_t> bytes) = 0;
};

// Everything the bridge does for one vehicle, without I/O or threads: bytes
// come in through on_bytes(), time through the Clock, and frames go out through
// the Transport. The owner calls tick() every 10 ms and never calls the session
// from two threads.
//
// - Lock. The session locks to the system id of the first HEARTBEAT from
//   component 1 that is not a GCS, taking only expected_sysid's when that is
//   set. Before the lock only such a heartbeat counts as traffic. Frames from
//   any other system id (from the start, when expected_sysid is set) are
//   counted in foreign_sysid_frames and dropped before the model or the link
//   monitor sees them.
// - Setup. Every Lost -> Recovering transition, the first one included,
//   forgets what the last setup learned and runs a new SetupSequence: version
//   request, then the adapter's message intervals. When it is done the link
//   monitor hears about it and vehicle_info is published.
// - Reboot. A restart quick enough that the link never reaches Lost shows up
//   as time_boot_ms going backwards (Changed::Reboot). It gets the same reset
//   and a new setup, and the link monitor forgets its sequence history, with
//   no change of link state.
// - Publishing. pose on each GLOBAL_POSITION_INT, capped at pose_hz.
//   vehicle_state and health at once on a discrete change, otherwise at their
//   rate; the periodic ones pause while the link is Lost, so stale values are
//   not repeated. link_status on every transition and at link_hz.
//   vehicle_info after each setup, and again if it changes later.
// - The bridge's GCS HEARTBEAT goes out at 1 Hz.
class VehicleSession {
 public:
  struct Counters {
    uint64_t foreign_sysid_frames = 0;
    // Setup runs started, counting the first and those after a reboot.
    uint64_t resetups = 0;
    // Restarts seen while the link was up (see Reboot above).
    uint64_t reboots = 0;
  };

  // Throws std::invalid_argument if the channel is not below mav::kMaxChannels
  // or a rate is not a positive number.
  VehicleSession(SessionConfig cfg, mav::Channel ch, Clock& clock,
                 Transport& transport, TelemetrySink& sink);

  // One datagram (or any slice of the byte stream) from the vehicle's link.
  void on_bytes(std::span<const uint8_t> bytes);
  // Link monitor, setup retries, GCS heartbeat and the periodic publishes.
  void tick();

  const Counters& counters() const { return counters_; }
  mav::LinkState link_state() const { return monitor_.state(); }

 private:
  // A fixed-period timer driven by tick(). It keeps its grid while ticked
  // often enough and restarts it after a longer gap.
  struct Schedule {
    std::chrono::nanoseconds period;
    std::optional<MonoTime> next;
    bool due(MonoTime now);
    void restart(MonoTime now) { next = now + period; }
  };

  // False if the frame must not reach the model or the monitor.
  bool accept(const mavlink_message_t& msg);
  void handle(const mavlink_message_t& msg, MonoTime now, uint64_t rt);
  void on_transition(const mav::LinkTransition& tr, MonoTime now);
  void begin_setup(MonoTime now);
  void drive_setup(MonoTime now);
  void send(const mavlink_message_t& msg);

  void publish_link();
  void publish_state() {
    sink_.vehicle_state(cfg_.vehicle_id, model_.state(), state_stamp_);
  }
  void publish_health() {
    sink_.health(cfg_.vehicle_id, model_.health(), health_stamp_);
  }
  void publish_info() {
    sink_.vehicle_info(cfg_.vehicle_id, model_.info(), info_stamp_);
  }

  SessionConfig cfg_;
  Clock& clock_;
  Transport& transport_;
  TelemetrySink& sink_;
  mav::Framer framer_;
  mav::Packer packer_;
  mav::LinkMonitor monitor_;
  VehicleModel model_;
  std::optional<mav::SetupSequence> setup_;
  // A setup has finished and none is running.
  bool setup_complete_ = false;
  std::optional<uint8_t> locked_sysid_;
  uint8_t locked_compid_ = 0;

  RateLimiter pose_limiter_;
  Schedule heartbeat_;
  Schedule link_pub_;
  Schedule state_pub_;
  Schedule health_pub_;

  MonoTime started_;
  bool have_state_ = false;
  bool have_health_ = false;
  // Decode time of the last message that fed each part.
  Stamp state_stamp_;
  Stamp health_stamp_;
  Stamp info_stamp_;
  Counters counters_;
};

}  // namespace skyrelay::vehicle
