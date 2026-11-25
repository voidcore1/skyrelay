#include "skyrelay/mav/framer.hpp"

namespace skyrelay::mav {

Framer::Framer(Channel ch) : ch_(ch) {
  mavlink_reset_channel_status(ch_);
}

std::vector<mavlink_message_t> Framer::push(std::span<const uint8_t> bytes) {
  std::vector<mavlink_message_t> out;
  mavlink_message_t msg{};
  mavlink_status_t status{};
  for (const uint8_t byte : bytes) {
    // In the idle state the parser silently ignores anything that is not a
    // frame start, so those bytes are counted here.
    const bool idle = mavlink_get_channel_status(ch_)->parse_state <=
                      MAVLINK_PARSE_STATE_IDLE;
    const bool is_stx = byte == MAVLINK_STX || byte == MAVLINK_STX_MAVLINK1;

    const uint8_t result = mavlink_frame_char(ch_, byte, &msg, &status);

    // packet_rx_drop_count carries this call's parse_error delta.
    stats_.bytes_discarded += status.packet_rx_drop_count;
    if (idle && !is_stx) {
      ++stats_.bytes_discarded;
    }
    switch (result) {
      case MAVLINK_FRAMING_OK:
        ++stats_.frames_ok;
        out.push_back(msg);
        break;
      case MAVLINK_FRAMING_BAD_CRC:
        ++stats_.crc_errors;
        break;
      case MAVLINK_FRAMING_BAD_SIGNATURE:
        ++stats_.signature_errors;
        break;
      default:
        break;
    }
  }
  return out;
}

}  // namespace skyrelay::mav
