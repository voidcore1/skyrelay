#include "skyrelay/mav/setup_sequence.hpp"

#include <cmath>

namespace skyrelay::mav {

namespace {
constexpr uint16_t kRequestMessage = 512;      // MAV_CMD_REQUEST_MESSAGE
constexpr uint16_t kSetMessageInterval = 511;  // MAV_CMD_SET_MESSAGE_INTERVAL
constexpr uint32_t kAutopilotVersion = 148;
}  // namespace

SetupSequence::SetupSequence(std::vector<IntervalRequest> intervals,
                             uint8_t target_sys, uint8_t target_comp,
                             MonoTime start) {
  steps_.push_back(
      {CommandSpec{kRequestMessage,
                   {static_cast<float>(kAutopilotVersion), 0, 0, 0, 0, 0, 0},
                   target_sys, target_comp},
       kAutopilotVersion});
  for (const auto& i : intervals) {
    const float us = static_cast<float>(std::llround(1e6 / i.hz));
    steps_.push_back({CommandSpec{kSetMessageInterval,
                                  {static_cast<float>(i.msg_id), us, 0, 0, 0,
                                   0, 0},
                                  target_sys, target_comp},
                      i.msg_id});
  }
  start_next(start);
}

void SetupSequence::start_next(MonoTime now) {
  current_.reset();
  if (next_ >= steps_.size()) return;
  const Step& s = steps_[next_++];
  current_.emplace(s.spec, now);
  current_msg_id_ = s.msg_id;
}

void SetupSequence::finish_if_terminal(MonoTime now) {
  if (!current_ || !current_->terminal()) return;
  if (current_->state() != TxState::Accepted) failed_.push_back(current_msg_id_);
  start_next(now);
}

std::optional<SendRequest> SetupSequence::poll(MonoTime now) {
  if (!current_) return std::nullopt;
  auto r = current_->poll(now);
  if (current_->terminal()) {
    finish_if_terminal(now);
    // The next step starts now, so it can send straight away.
    if (current_) r = current_->poll(now);
  }
  return r;
}

void SetupSequence::on_ack(const mavlink_command_ack_t& ack, uint8_t own_sysid,
                           MonoTime now) {
  if (!current_) return;
  current_->on_ack(ack, own_sysid, now);
  finish_if_terminal(now);
}

}  // namespace skyrelay::mav
