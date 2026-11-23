#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include "skyrelay/common/clock.hpp"
#include "skyrelay/vehicle/adapter.hpp"
#include "skyrelay/vehicle/session.hpp"
#include "support/recording_sink.hpp"
#include "support/sim_vehicle.hpp"

using namespace skyrelay;
using namespace skyrelay::vehicle;
using skyrelay::mav::LinkState;
using skyrelay::test::FrameParser;
using skyrelay::test::RecordingSink;
using skyrelay::test::SimVehicle;
using namespace std::chrono_literals;

namespace {

// Realtime at the start of every test, so a realtime stamp can never be
// mistaken for a monotonic one.
constexpr uint64_t kRealtimeStartNs = 1'790'000'000'000'000'000;
constexpr int32_t kLatA = 473977420;
constexpr int32_t kLatB = 100000000;

class CapturingTransport final : public Transport {
 public:
  void send(std::span<const uint8_t> bytes) override {
    frames.emplace_back(bytes.begin(), bytes.end());
  }

  // Every message sent so far, decoded.
  std::vector<mavlink_message_t> messages() const {
    FrameParser p;
    std::vector<mavlink_message_t> out;
    for (const auto& f : frames) {
      for (const auto& m : p.push(f)) out.push_back(m);
    }
    return out;
  }

  std::vector<mavlink_command_long_t> commands() const {
    std::vector<mavlink_command_long_t> out;
    for (const auto& m : messages()) {
      if (m.msgid != MAVLINK_MSG_ID_COMMAND_LONG) continue;
      mavlink_command_long_t c;
      mavlink_msg_command_long_decode(&m, &c);
      out.push_back(c);
    }
    return out;
  }

  std::vector<std::vector<uint8_t>> frames;
};

SessionConfig default_config() {
  SessionConfig c{};
  c.vehicle_id = 7;
  return c;
}

// Drives a session against scripted vehicles in 10 ms steps of fake time. In
// each step the vehicles' scheduled traffic goes in as one datagram, the
// session ticks, and whatever the session sent is answered at once.
class Harness {
 public:
  explicit Harness(SessionConfig cfg = default_config())
      : session(cfg, 0, clock, transport, sink) {
    clock.set_realtime_ns(kRealtimeStartNs);
  }

  SimVehicle& add_vehicle(
      uint8_t sysid, int32_t lat_e7 = kLatA,
      std::chrono::milliseconds position_period = 50ms) {
    vehicles.push_back(std::make_unique<SimVehicle>(sysid, clock.mono(), lat_e7,
                                                    position_period));
    return *vehicles.back();
  }

  void run_for(std::chrono::milliseconds d) {
    const MonoTime end = clock.mono() + d;
    while (clock.mono() < end) {
      std::vector<uint8_t> bytes;
      for (auto& v : vehicles) {
        const auto b = v->step(clock.mono());
        bytes.insert(bytes.end(), b.begin(), b.end());
      }
      if (!bytes.empty()) session.on_bytes(bytes);
      session.tick();
      deliver();
      clock.advance(10ms);
    }
  }

  // Answers everything the session has sent that the vehicles have not seen.
  void deliver() {
    while (delivered_ < transport.frames.size()) {
      const std::vector<uint8_t> frame = transport.frames[delivered_++];
      std::vector<uint8_t> reply;
      for (auto& v : vehicles) {
        const auto r = v->receive(frame);
        reply.insert(reply.end(), r.begin(), r.end());
      }
      if (!reply.empty()) session.on_bytes(reply);
    }
  }

  FakeClock clock;
  CapturingTransport transport;
  RecordingSink sink;
  VehicleSession session;
  std::vector<std::unique_ptr<SimVehicle>> vehicles;

 private:
  size_t delivered_ = 0;
};

uint64_t ns(std::chrono::nanoseconds d) {
  return static_cast<uint64_t>(d.count());
}

}  // namespace

TEST(Session, ConnectsAndPublishesPoseAt20Hz) {
  Harness h;
  h.add_vehicle(1);
  h.run_for(3s);

  EXPECT_EQ(h.session.link_state(), LinkState::Connected);

  const uint64_t last_second = h.clock.realtime_ns() - ns(1s);
  int recent = 0;
  for (const auto& p : h.sink.poses) {
    EXPECT_EQ(p.vehicle_id, 7u);
    if (p.stamp.t_bridge_ns >= last_second) ++recent;
  }
  EXPECT_GE(recent, 19);
  EXPECT_LE(recent, 21);

  ASSERT_EQ(h.sink.infos.size(), 1u);
  EXPECT_EQ(h.sink.infos[0].data.autopilot, Autopilot::Px4);
  EXPECT_EQ(h.sink.infos[0].data.firmware_version, "1.17.0");
  EXPECT_EQ(h.sink.infos[0].data.mav_sysid, 1);
}

TEST(Session, PoseCarriesSourceAndBridgeTime) {
  Harness h;
  auto& v = h.add_vehicle(1);
  h.run_for(3s);

  ASSERT_FALSE(h.sink.poses.empty());
  const auto& p = h.sink.poses.back();
  // The last GLOBAL_POSITION_INT went out at 2950 ms and was decoded in the
  // same step.
  EXPECT_EQ(p.data.time_boot_ms, 2950u);
  EXPECT_EQ(p.stamp.t_bridge_ns, kRealtimeStartNs + ns(2950ms));
  EXPECT_EQ(p.stamp.t_source_us, v.source_time_us(2950));
}

TEST(Session, CapsPoseAtPoseHz) {
  Harness h;
  h.add_vehicle(1, kLatA, 10ms);  // 100 Hz
  h.run_for(3s);

  const uint64_t last_second = h.clock.realtime_ns() - ns(1s);
  int recent = 0;
  for (const auto& p : h.sink.poses) {
    if (p.stamp.t_bridge_ns >= last_second) ++recent;
  }
  EXPECT_GE(recent, 19);
  EXPECT_LE(recent, 21);
}

TEST(Session, SendsGcsHeartbeat1Hz) {
  Harness h;
  h.add_vehicle(1);
  h.run_for(3s);

  int heartbeats = 0;
  for (const auto& m : h.transport.messages()) {
    if (m.msgid != MAVLINK_MSG_ID_HEARTBEAT) continue;
    mavlink_heartbeat_t hb;
    mavlink_msg_heartbeat_decode(&m, &hb);
    EXPECT_EQ(m.sysid, 245u);
    EXPECT_EQ(m.compid, MAV_COMP_ID_MISSIONPLANNER);
    EXPECT_EQ(hb.type, MAV_TYPE_GCS);
    ++heartbeats;
  }
  EXPECT_EQ(heartbeats, 3);
}

TEST(Session, RequestsIntervalsDuringSetup) {
  Harness h;
  h.add_vehicle(1);
  h.run_for(1s);

  const auto cmds = h.transport.commands();
  const auto intervals = Px4Adapter{}.telemetry_intervals();
  ASSERT_EQ(cmds.size(), intervals.size() + 1);

  EXPECT_EQ(cmds[0].command, MAV_CMD_REQUEST_MESSAGE);
  EXPECT_FLOAT_EQ(cmds[0].param1, 148.f);
  for (size_t i = 0; i < intervals.size(); ++i) {
    const auto& c = cmds[i + 1];
    EXPECT_EQ(c.command, MAV_CMD_SET_MESSAGE_INTERVAL) << i;
    EXPECT_FLOAT_EQ(c.param1, static_cast<float>(intervals[i].msg_id)) << i;
    EXPECT_FLOAT_EQ(c.param2,
                    static_cast<float>(std::llround(1e6 / intervals[i].hz)))
        << i;
  }
  for (const auto& c : cmds) {
    EXPECT_EQ(c.target_system, 1);
    EXPECT_EQ(c.target_component, MAV_COMP_ID_AUTOPILOT1);
  }
}

// Review Focus 1: a second vehicle on the same port must never leak into this
// vehicle's state.
TEST(Session, IgnoresForeignSysid) {
  Harness h;
  h.add_vehicle(1, kLatA);
  auto& other = h.add_vehicle(9, kLatB);
  other.set_armed(true);
  h.run_for(3s);

  EXPECT_EQ(h.session.link_state(), LinkState::Connected);
  ASSERT_FALSE(h.sink.poses.empty());
  for (const auto& p : h.sink.poses) {
    EXPECT_DOUBLE_EQ(p.data.lat_deg, kLatA * 1e-7);
  }
  for (const auto& s : h.sink.states) {
    EXPECT_EQ(s.data.armed, false);
  }
  EXPECT_GT(h.session.counters().foreign_sysid_frames, 0u);
  EXPECT_TRUE(other.commands().empty());

  // The other vehicle's traffic must not keep this vehicle's link alive.
  h.vehicles[0]->set_online(false);
  h.run_for(1200ms);
  EXPECT_EQ(h.session.link_state(), LinkState::Lost);
}

TEST(Session, ExpectedSysidLocksEvenWhenAnotherSpeaksFirst) {
  SessionConfig cfg = default_config();
  cfg.expected_sysid = 1;
  Harness h(cfg);
  h.add_vehicle(9, kLatB);  // first in every datagram
  h.add_vehicle(1, kLatA);
  h.run_for(3s);

  EXPECT_EQ(h.session.link_state(), LinkState::Connected);
  ASSERT_FALSE(h.sink.poses.empty());
  for (const auto& p : h.sink.poses) {
    EXPECT_DOUBLE_EQ(p.data.lat_deg, kLatA * 1e-7);
  }
  ASSERT_EQ(h.sink.infos.size(), 1u);
  EXPECT_EQ(h.sink.infos[0].data.mav_sysid, 1);
  EXPECT_GT(h.session.counters().foreign_sysid_frames, 0u);
}

TEST(Session, LinkLossAndRecovery) {
  Harness h;
  auto& v = h.add_vehicle(1);
  h.run_for(3s);
  ASSERT_EQ(h.session.link_state(), LinkState::Connected);

  v.set_online(false);
  h.run_for(1200ms);
  EXPECT_EQ(h.session.link_state(), LinkState::Lost);

  v.set_online(true);
  h.run_for(2s);

  EXPECT_EQ(h.sink.link_states(),
            (std::vector<LinkState>{LinkState::Recovering, LinkState::Connected,
                                    LinkState::Degraded, LinkState::Lost,
                                    LinkState::Recovering,
                                    LinkState::Connected}));
  EXPECT_EQ(h.session.counters().resetups, 2u);
}

// Review Focus 3: a reboot is a gap, then seq and time_boot_ms from zero.
TEST(Session, RebootTriggersResetup) {
  Harness h;
  auto& v = h.add_vehicle(1);
  h.run_for(3s);
  ASSERT_EQ(h.session.link_state(), LinkState::Connected);

  v.set_online(false);
  h.run_for(3s);
  v.reboot(h.clock.mono());
  const uint64_t rebooted_at = h.clock.realtime_ns();
  v.set_online(true);
  h.run_for(2s);

  EXPECT_EQ(h.session.link_state(), LinkState::Connected);
  EXPECT_EQ(h.session.counters().resetups, 2u);

  int version_requests = 0;
  for (const auto& c : h.transport.commands()) {
    if (c.command == MAV_CMD_REQUEST_MESSAGE) ++version_requests;
  }
  EXPECT_EQ(version_requests, 2);

  ASSERT_EQ(h.sink.infos.size(), 2u);
  EXPECT_EQ(h.sink.infos[1].data.firmware_version, "1.17.0");

  ASSERT_FALSE(h.sink.links.empty());
  EXPECT_LT(h.sink.links.back().data.stats.loss_pct, 1.0);

  // No pose after the reboot may be dated with the offset learned before it:
  // until the new SYSTEM_TIME the source time is unknown (0).
  int dated = 0;
  for (const auto& p : h.sink.poses) {
    if (p.stamp.t_bridge_ns < rebooted_at) continue;
    EXPECT_LT(p.data.time_boot_ms, 2000u);
    if (p.stamp.t_source_us == 0) continue;
    EXPECT_EQ(p.stamp.t_source_us, v.source_time_us(p.data.time_boot_ms));
    ++dated;
  }
  EXPECT_GT(dated, 0);
}

TEST(Session, StateChangePublishedImmediately) {
  Harness h;
  auto& v = h.add_vehicle(1);
  h.run_for(3s);
  const size_t before = h.sink.states.size();
  ASSERT_GT(before, 0u);
  ASSERT_EQ(h.sink.states.back().data.armed, false);

  v.set_armed(true);
  h.session.on_bytes(v.heartbeat_bytes());

  ASSERT_EQ(h.sink.states.size(), before + 1);
  EXPECT_EQ(h.sink.states.back().data.armed, true);
}

TEST(Session, LinkStatusSinceIsRealtime) {
  Harness h;
  h.add_vehicle(1);
  h.run_for(3s);

  // The sample published on the transition carries the transition time; the
  // periodic ones after it keep that time.
  const LinkStatusData* first = nullptr;
  for (const auto& l : h.sink.links) {
    if (l.data.state == LinkState::Connected) {
      first = &l.data;
      EXPECT_EQ(l.data.state_since_ns, l.stamp.t_bridge_ns);
      break;
    }
  }
  ASSERT_NE(first, nullptr);
  EXPECT_GT(first->state_since_ns, kRealtimeStartNs);
  ASSERT_EQ(h.sink.links.back().data.state, LinkState::Connected);
  EXPECT_EQ(h.sink.links.back().data.state_since_ns, first->state_since_ns);
  EXPECT_LT(h.sink.links.back().stamp.t_bridge_ns - first->state_since_ns,
            ns(3s));
}

TEST(Session, RejectsChannelOutOfRange) {
  FakeClock clock;
  CapturingTransport transport;
  RecordingSink sink;
  EXPECT_THROW(VehicleSession(default_config(), mav::kMaxChannels, clock,
                              transport, sink),
               std::invalid_argument);
}
