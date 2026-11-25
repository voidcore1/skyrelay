#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "skyrelay/mav/command_transaction.hpp"
#include "skyrelay/mav/mavlink.hpp"

namespace skyrelay::mav {

// Builds outgoing MAVLink messages for one session. The sequence number lives
// in the per-channel library state, so two Packers on different channels count
// independently. Not thread-safe; one Packer per session.
class Packer {
 public:
  Packer(Channel ch, uint8_t sysid, uint8_t compid);

  // HEARTBEAT as a ground station: MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID.
  mavlink_message_t gcs_heartbeat();

  mavlink_message_t command_long(uint8_t target_sys, uint8_t target_comp,
                                 uint16_t command,
                                 const std::array<float, 7>& params,
                                 uint8_t confirmation);

  // COMMAND_LONG with the request's confirmation, or COMMAND_INT with current
  // and autocontinue 0, as the spec's encoding says.
  mavlink_message_t command(const SendRequest& req);

  // Wire encoding of a message.
  static std::vector<uint8_t> serialize(const mavlink_message_t& msg);

 private:
  Channel ch_;
  uint8_t sysid_;
  uint8_t compid_;
};

}  // namespace skyrelay::mav
