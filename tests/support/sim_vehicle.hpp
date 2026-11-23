#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

#include "skyrelay/common/clock.hpp"
#include "skyrelay/mav/mavlink.hpp"

namespace skyrelay::test {

// Splits a byte stream into MAVLink messages. It keeps its own parser state
// instead of using a channel, so it can never disturb the channel state of the
// session under test.
class FrameParser {
 public:
  std::vector<mavlink_message_t> push(std::span<const uint8_t> bytes) {
    std::vector<mavlink_message_t> out;
    for (const uint8_t b : bytes) {
      mavlink_message_t msg{};
      mavlink_status_t st{};
      if (mavlink_frame_char_buffer(&buf_, &status_, b, &msg, &st) ==
          MAVLINK_FRAMING_OK) {
        out.push_back(msg);
      }
    }
    return out;
  }

 private:
  mavlink_message_t buf_{};
  mavlink_status_t status_{};
};

// A scripted PX4-like vehicle on the other end of a session. It sends
// HEARTBEAT and SYSTEM_TIME at 1 Hz and GLOBAL_POSITION_INT at 20 Hz (or the
// period given), answers
// every COMMAND_LONG addressed to it with ACCEPTED, and sends AUTOPILOT_VERSION
// (PX4 1.17.0) before the ack when asked for it. It does not change its rates
// on SET_MESSAGE_INTERVAL.
//
// The sequence counter is its own mavlink_status_t, not a channel, so a reboot
// can restart it at 0.
class SimVehicle {
 public:
  // Unix time, in microseconds, when the simulation starts.
  static constexpr uint64_t kUnixStartUs = 1'790'000'000'000'000;

  SimVehicle(uint8_t sysid, MonoTime start, int32_t lat_e7 = 473977420,
             std::chrono::milliseconds position_period =
                 std::chrono::milliseconds{50})
      : sysid_(sysid),
        start_(start),
        boot_(start),
        lat_e7_(lat_e7),
        position_period_(position_period) {
    reschedule(start);
  }

  // Everything that falls due at or before `now`, as one datagram. After a
  // pause it sends one of each and picks its schedule up from `now`, like a
  // vehicle that kept running while its link was down.
  std::vector<uint8_t> step(MonoTime now) {
    std::vector<uint8_t> out;
    if (!online_) return out;
    if (due(next_hb_, std::chrono::seconds{1}, now)) append(out, heartbeat());
    if (due(next_pos_, position_period_, now)) {
      mavlink_message_t m{};
      mavlink_msg_global_position_int_pack_status(
          sysid_, kComp, &tx_, &m, boot_ms(now), lat_e7_, 85455940, 488000,
          10000, 100, -50, 0, 9000);
      append(out, m);
    }
    // After the position, so the first position after a reboot arrives
    // before the vehicle has said what time it is.
    if (due(next_time_, std::chrono::seconds{1}, now)) {
      mavlink_message_t m{};
      mavlink_msg_system_time_pack_status(sysid_, kComp, &tx_, &m,
                                          unix_us(now), boot_ms(now));
      append(out, m);
    }
    return out;
  }

  // Bytes from the ground station; returns the vehicle's replies.
  std::vector<uint8_t> receive(std::span<const uint8_t> bytes) {
    std::vector<uint8_t> out;
    if (!online_) return out;
    for (const auto& msg : rx_.push(bytes)) {
      if (msg.msgid != MAVLINK_MSG_ID_COMMAND_LONG) continue;
      mavlink_command_long_t c;
      mavlink_msg_command_long_decode(&msg, &c);
      if (c.target_system != sysid_ && c.target_system != 0) continue;
      commands_.push_back(c);
      if (c.command == MAV_CMD_REQUEST_MESSAGE &&
          c.param1 == static_cast<float>(MAVLINK_MSG_ID_AUTOPILOT_VERSION)) {
        append(out, autopilot_version());
      }
      mavlink_message_t ack{};
      mavlink_msg_command_ack_pack_status(
          sysid_, kComp, &tx_, &ack, c.command, MAV_RESULT_ACCEPTED, 0, 0,
          static_cast<uint8_t>(msg.sysid), msg.compid);
      append(out, ack);
    }
    return out;
  }

  // One HEARTBEAT right now, outside the schedule.
  std::vector<uint8_t> heartbeat_bytes() {
    std::vector<uint8_t> out;
    append(out, heartbeat());
    return out;
  }

  // Power cycle: sequence numbers and time_boot_ms start again from 0.
  void reboot(MonoTime now) {
    tx_ = {};
    boot_ = now;
    reschedule(now);
  }

  void set_armed(bool armed) { armed_ = armed; }
  // Offline, the vehicle neither sends nor hears anything: a link outage.
  void set_online(bool online) { online_ = online; }

  uint8_t sysid() const { return sysid_; }
  // Sequence number of the next frame the vehicle sends.
  uint8_t next_seq() const { return tx_.current_tx_seq; }
  int32_t lat_e7() const { return lat_e7_; }
  // Vehicle UTC for a time_boot_ms stamp of the current boot.
  uint64_t source_time_us(uint32_t time_boot_ms) const {
    return unix_us(boot_) + uint64_t{time_boot_ms} * 1000;
  }
  const std::vector<mavlink_command_long_t>& commands() const {
    return commands_;
  }

 private:
  static constexpr uint8_t kComp = MAV_COMP_ID_AUTOPILOT1;

  void reschedule(MonoTime now) {
    next_hb_ = now;
    next_time_ = now;
    next_pos_ = now;
  }

  // Keeps a fixed grid while called often enough, and restarts it after a gap
  // longer than one period instead of sending the backlog.
  static bool due(MonoTime& next, std::chrono::nanoseconds period,
                  MonoTime now) {
    if (now < next) return false;
    next = now < next + period ? next + period : now + period;
    return true;
  }

  uint32_t boot_ms(MonoTime now) const {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - boot_)
            .count());
  }
  uint64_t unix_us(MonoTime now) const {
    return kUnixStartUs +
           static_cast<uint64_t>(
               std::chrono::duration_cast<std::chrono::microseconds>(now -
                                                                     start_)
                   .count());
  }

  mavlink_message_t heartbeat() {
    // POSCTL: PX4 main mode 3 in byte 2 of custom_mode.
    uint8_t base = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;
    if (armed_) base |= MAV_MODE_FLAG_SAFETY_ARMED;
    mavlink_message_t m{};
    mavlink_msg_heartbeat_pack_status(
        sysid_, kComp, &tx_, &m, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_PX4, base,
        3u << 16, armed_ ? MAV_STATE_ACTIVE : MAV_STATE_STANDBY);
    return m;
  }

  mavlink_message_t autopilot_version() {
    const uint32_t v1_17_0 = (1u << 24) | (17u << 16) | (0u << 8) | 255u;
    const uint8_t zero8[8] = {};
    const uint8_t zero18[18] = {};
    mavlink_message_t m{};
    mavlink_msg_autopilot_version_pack_status(
        sysid_, kComp, &tx_, &m, MAV_PROTOCOL_CAPABILITY_MAVLINK2, v1_17_0, 0,
        0, 0, zero8, zero8, zero8, 0, 0, 0, zero18);
    return m;
  }

  static void append(std::vector<uint8_t>& out, const mavlink_message_t& m) {
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    const uint16_t n = mavlink_msg_to_send_buffer(buf, &m);
    out.insert(out.end(), buf, buf + n);
  }
  static void append(std::vector<uint8_t>& out,
                     const std::vector<uint8_t>& more) {
    out.insert(out.end(), more.begin(), more.end());
  }

  uint8_t sysid_;
  MonoTime start_;
  MonoTime boot_;
  int32_t lat_e7_;
  std::chrono::milliseconds position_period_;
  bool armed_ = false;
  bool online_ = true;
  mavlink_status_t tx_{};
  FrameParser rx_;
  MonoTime next_hb_{};
  MonoTime next_time_{};
  MonoTime next_pos_{};
  std::vector<mavlink_command_long_t> commands_;
};

}  // namespace skyrelay::test
