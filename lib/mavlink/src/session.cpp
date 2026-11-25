#include "skyrelay/vehicle/session.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace skyrelay::vehicle {

namespace {

mav::Channel checked_channel(mav::Channel ch) {
  if (ch >= mav::kMaxChannels) {
    throw std::invalid_argument("MAVLink channel " + std::to_string(ch) +
                                " is out of range (0-" +
                                std::to_string(mav::kMaxChannels - 1) + ")");
  }
  return ch;
}

double checked_rate(double hz, const char* name) {
  if (!std::isfinite(hz) || hz < 1e-9) {
    throw std::invalid_argument(std::string(name) +
                                " must be a positive number");
  }
  return hz;
}

std::chrono::nanoseconds period_of(double hz, const char* name) {
  const double ns = 1e9 / checked_rate(hz, name);
  return std::chrono::nanoseconds{
      std::max(1LL, static_cast<long long>(std::llround(ns)))};
}

// A component that is not a flight controller (a gimbal or companion
// computer, say) reports MAV_AUTOPILOT_INVALID, so that does not count either.
bool is_autopilot_heartbeat(const mavlink_message_t& msg) {
  return msg.msgid == MAVLINK_MSG_ID_HEARTBEAT &&
         msg.compid == MAV_COMP_ID_AUTOPILOT1 &&
         mavlink_msg_heartbeat_get_type(&msg) != MAV_TYPE_GCS &&
         mavlink_msg_heartbeat_get_autopilot(&msg) != MAV_AUTOPILOT_INVALID;
}

}  // namespace

bool VehicleSession::Schedule::due(MonoTime now) {
  if (next && now < *next) return false;
  next = (next && now < *next + period) ? *next + period : now + period;
  return true;
}

VehicleSession::VehicleSession(SessionConfig cfg, mav::Channel ch,
                               Clock& clock, Transport& transport,
                               TelemetrySink& sink)
    : cfg_(cfg),
      clock_(clock),
      transport_(transport),
      sink_(sink),
      // The framer is built first, so a bad channel throws before the
      // library's per-channel arrays are indexed with it.
      framer_(checked_channel(ch)),
      packer_(ch, cfg.gcs_sysid, MAV_COMP_ID_MISSIONPLANNER),
      monitor_(cfg.link),
      pose_limiter_(checked_rate(cfg.rates.pose_hz, "pose_hz")),
      heartbeat_{std::chrono::seconds{1}, std::nullopt},
      link_pub_{period_of(cfg.rates.link_hz, "link_hz"), std::nullopt},
      state_pub_{period_of(cfg.rates.state_hz, "state_hz"), std::nullopt},
      health_pub_{period_of(cfg.rates.health_hz, "health_hz"), std::nullopt},
      started_(clock.mono()) {}

bool VehicleSession::on_bytes(std::span<const uint8_t> bytes) {
  // Frames in one datagram arrived together, so they share one decode time.
  const MonoTime now = clock_.mono();
  const uint64_t rt = clock_.realtime_ns();
  bool from_vehicle = false;
  for (const auto& msg : framer_.push(bytes)) {
    from_vehicle = handle(msg, now, rt) || from_vehicle;
  }
  return from_vehicle;
}

bool VehicleSession::accept(const mavlink_message_t& msg) {
  if (locked_sysid_) {
    if (msg.sysid == *locked_sysid_) return true;
    ++counters_.foreign_sysid_frames;
    return false;
  }
  if (cfg_.expected_sysid && msg.sysid != *cfg_.expected_sysid) {
    ++counters_.foreign_sysid_frames;
    return false;
  }
  // Until the autopilot has announced itself nothing counts as this
  // vehicle's traffic. System id 0 is the broadcast address, never a sender.
  if (!is_autopilot_heartbeat(msg) || msg.sysid == 0) return false;
  locked_sysid_ = msg.sysid;
  locked_compid_ = msg.compid;
  return true;
}

bool VehicleSession::handle(const mavlink_message_t& msg, MonoTime now,
                            uint64_t rt) {
  if (!accept(msg)) return false;

  bool resetup = false;
  if (auto tr = monitor_.on_message(now, msg.compid, msg.seq)) {
    on_transition(*tr, now);
    if (tr->from == mav::LinkState::Lost &&
        tr->to == mav::LinkState::Recovering) {
      // The vehicle may have rebooted or been reflashed while the link was
      // down, so firmware and the time offset are learned again.
      model_.reset_for_resetup();
      resetup = true;
    }
  }
  const Changed ch = model_.apply(msg);
  if (has(ch, Changed::Reboot)) {
    // Back before the link was lost: the vehicle restarted with its message
    // intervals gone and its seq at 0, so it gets the reconnect treatment
    // while the link state keeps following the traffic.
    ++counters_.reboots;
    model_.reset_for_resetup();
    monitor_.on_peer_restart(now);
    resetup = true;
  }
  // After apply(): on the first connect this heartbeat is what picks the
  // adapter whose interval list the setup sends.
  if (resetup) begin_setup(now);

  if (msg.msgid == MAVLINK_MSG_ID_COMMAND_ACK && setup_) {
    mavlink_command_ack_t ack;
    mavlink_msg_command_ack_decode(&msg, &ack);
    setup_->on_ack(ack, cfg_.gcs_sysid, now);
    drive_setup(now);
  }

  if (has(ch, Changed::Pose) &&
      msg.msgid == MAVLINK_MSG_ID_GLOBAL_POSITION_INT &&
      pose_limiter_.allow(now)) {
    const PoseData& p = model_.pose();
    sink_.pose(cfg_.vehicle_id, p,
               Stamp{model_.source_time_us(p.time_boot_ms), rt});
  }
  if (has(ch, Changed::State)) {
    have_state_ = true;
    state_stamp_ = Stamp{0, rt};
    if (model_.state_changed_discrete()) {
      publish_state();
      state_pub_.restart(now);
    }
  }
  if (has(ch, Changed::Health)) {
    have_health_ = true;
    health_stamp_ = Stamp{0, rt};
    if (model_.health_changed_discrete()) {
      publish_health();
      health_pub_.restart(now);
    }
  }
  if (has(ch, Changed::Info)) {
    info_stamp_ = Stamp{0, rt};
    // During a setup the info goes out once, when the setup is done.
    if (setup_complete_) publish_info();
  }
  return true;
}

void VehicleSession::on_transition(const mav::LinkTransition& tr,
                                   MonoTime now) {
  if (tr.to == mav::LinkState::Lost) {
    // Commands into a dead link only time out; the next Lost -> Recovering
    // starts a fresh setup anyway.
    setup_.reset();
    // What the vehicle said before the outage is not repeated after it: the
    // periodic state and health publishes wait for a fresh message.
    have_state_ = false;
    have_health_ = false;
  }
  publish_link();
  link_pub_.restart(now);
}

void VehicleSession::begin_setup(MonoTime now) {
  ++counters_.resetups;
  setup_complete_ = false;
  const AutopilotAdapter* adapter = model_.adapter();
  setup_.emplace(adapter ? adapter->telemetry_intervals()
                         : std::vector<mav::IntervalRequest>{},
                 *locked_sysid_, locked_compid_, now);
  drive_setup(now);
}

void VehicleSession::drive_setup(MonoTime now) {
  if (!setup_) return;
  while (auto req = setup_->poll(now)) {
    send(packer_.command_long(req->spec.target_sys, req->spec.target_comp,
                              req->spec.command, req->spec.params,
                              req->confirmation));
  }
  if (!setup_->done()) return;
  setup_.reset();
  setup_complete_ = true;
  monitor_.on_setup_complete(now);
  publish_info();
}

void VehicleSession::tick() {
  const MonoTime now = clock_.mono();
  if (auto tr = monitor_.tick(now)) on_transition(*tr, now);
  drive_setup(now);
  if (heartbeat_.due(now)) send(packer_.gcs_heartbeat());
  if (link_pub_.due(now)) publish_link();
  // Stale values are not repeated once the vehicle is gone.
  const bool live = monitor_.state() != mav::LinkState::Lost;
  if (live && have_state_ && state_pub_.due(now)) publish_state();
  if (live && have_health_ && health_pub_.due(now)) publish_health();
}

void VehicleSession::send(const mavlink_message_t& msg) {
  const auto bytes = mav::Packer::serialize(msg);
  transport_.send(bytes);
}

void VehicleSession::publish_link() {
  // Both clocks are read together; the monotonic state_since is carried over
  // to realtime by its age. Before the first message the monitor's Lost has no
  // real start, so it dates from the session's.
  const MonoTime now = clock_.mono();
  const uint64_t rt = clock_.realtime_ns();
  const MonoTime since = std::max(monitor_.state_since(), started_);
  const uint64_t age =
      now > since
          ? static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now -
                                                                     since)
                    .count())
          : 0;
  LinkStatusData d;
  d.state = monitor_.state();
  d.state_since_ns = age < rt ? rt - age : 0;
  d.stats = monitor_.stats(now);
  sink_.link_status(cfg_.vehicle_id, d, Stamp{0, rt});
}

}  // namespace skyrelay::vehicle
