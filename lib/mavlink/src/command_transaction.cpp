#include "skyrelay/mav/command_transaction.hpp"

namespace skyrelay::mav {

namespace {
// Retry offsets from the start; index i is the send with confirmation i + 1.
constexpr std::chrono::milliseconds kRetryAt[] = {
    std::chrono::milliseconds{350}, std::chrono::milliseconds{700}};
}  // namespace

CommandTransaction::CommandTransaction(
    CommandSpec spec, MonoTime start, std::chrono::milliseconds deadline,
    std::chrono::milliseconds in_progress_max)
    : spec_(spec),
      start_(start),
      deadline_at_(start + deadline),
      in_progress_max_(in_progress_max) {}

bool CommandTransaction::terminal() const {
  return state_ == TxState::Accepted || state_ == TxState::Denied ||
         state_ == TxState::Failed || state_ == TxState::TimedOut ||
         state_ == TxState::Cancelled;
}

std::optional<SendRequest> CommandTransaction::poll(MonoTime now) {
  if (terminal()) return std::nullopt;
  if (now >= deadline_at_) {
    state_ = TxState::TimedOut;
    return std::nullopt;
  }
  if (state_ == TxState::Pending) {
    state_ = TxState::Sent;
    sent_ = 1;
    return SendRequest{spec_, 0};
  }
  // After IN_PROGRESS the vehicle has the command; only the deadline matters.
  if (state_ == TxState::Sent && sent_ <= std::size(kRetryAt) &&
      now >= start_ + kRetryAt[sent_ - 1]) {
    return SendRequest{spec_, sent_++};
  }
  return std::nullopt;
}

void CommandTransaction::on_ack(const mavlink_command_ack_t& ack,
                                uint8_t own_sysid, MonoTime /*now*/) {
  if (terminal() || state_ == TxState::Pending) return;
  if (ack.command != spec_.command) return;
  if (ack.target_system != own_sysid && ack.target_system != 0) return;

  result_ = ack.result;
  switch (ack.result) {
    case MAV_RESULT_ACCEPTED:
      state_ = TxState::Accepted;
      break;
    case MAV_RESULT_IN_PROGRESS:
      state_ = TxState::InProgress;
      deadline_at_ = start_ + in_progress_max_;
      progress_ = ack.progress == UINT8_MAX
                      ? std::nullopt
                      : std::optional<uint8_t>{ack.progress};
      break;
    case MAV_RESULT_CANCELLED:
      state_ = TxState::Cancelled;
      break;
    case MAV_RESULT_TEMPORARILY_REJECTED:
    case MAV_RESULT_DENIED:
    case MAV_RESULT_UNSUPPORTED:
    case MAV_RESULT_COMMAND_LONG_ONLY:
    case MAV_RESULT_COMMAND_INT_ONLY:
    case MAV_RESULT_COMMAND_UNSUPPORTED_MAV_FRAME:
      state_ = TxState::Denied;
      break;
    default:  // FAILED and anything unknown
      state_ = TxState::Failed;
      break;
  }
}

}  // namespace skyrelay::mav
