#include "skyrelay/mav/packer.hpp"

namespace skyrelay::mav {

Packer::Packer(Channel ch, uint8_t sysid, uint8_t compid)
    : ch_(ch), sysid_(sysid), compid_(compid) {}

mavlink_message_t Packer::gcs_heartbeat() {
  mavlink_message_t msg{};
  mavlink_msg_heartbeat_pack_chan(sysid_, compid_, ch_, &msg, MAV_TYPE_GCS,
                                  MAV_AUTOPILOT_INVALID, 0, 0,
                                  MAV_STATE_ACTIVE);
  return msg;
}

mavlink_message_t Packer::command_long(uint8_t target_sys, uint8_t target_comp,
                                       uint16_t command,
                                       const std::array<float, 7>& p,
                                       uint8_t confirmation) {
  mavlink_message_t msg{};
  mavlink_msg_command_long_pack_chan(sysid_, compid_, ch_, &msg, target_sys,
                                     target_comp, command, confirmation, p[0],
                                     p[1], p[2], p[3], p[4], p[5], p[6]);
  return msg;
}

std::vector<uint8_t> Packer::serialize(const mavlink_message_t& msg) {
  std::vector<uint8_t> buf(MAVLINK_MAX_PACKET_LEN);
  const uint16_t n = mavlink_msg_to_send_buffer(buf.data(), &msg);
  buf.resize(n);
  return buf;
}

}  // namespace skyrelay::mav
