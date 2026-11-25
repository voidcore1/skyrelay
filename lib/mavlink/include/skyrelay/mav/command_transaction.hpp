#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

#include "skyrelay/common/clock.hpp"
#include "skyrelay/mav/mavlink.hpp"

namespace skyrelay::mav {

enum class CommandEncoding : uint8_t { Long, Int };

// One MAVLink command. COMMAND_LONG sends params[0..6] as param1-7.
// COMMAND_INT sends params[0..3] as param1-4, frame, x and y (degE7, INT32_MAX
// for "keep the current value") and params[6] as z; params[4..5] are unused.
struct CommandSpec {
  uint16_t command;
  std::array<float, 7> params{};
  uint8_t target_sys;
  uint8_t target_comp;
  CommandEncoding encoding = CommandEncoding::Long;
  uint8_t frame = 0;
  int32_t x = 0;
  int32_t y = 0;
};

enum class TxState : uint8_t {
  Pending,
  Sent,
  InProgress,
  Accepted,
  Denied,
  Failed,
  TimedOut,
  Cancelled
};

// What to put on the wire; the caller packs it with Packer::command.
// confirmation only exists in COMMAND_LONG; a COMMAND_INT retry is an
// identical copy.
struct SendRequest {
  CommandSpec spec;
  uint8_t confirmation;
};

// One MAVLink command with retries, matched against its COMMAND_ACK. It never
// reads a clock or does I/O: time comes in through the arguments and poll()
// says what to send. Retries go out at 350 ms and 700 ms with confirmation 1
// and 2; the transaction times out at the deadline. An IN_PROGRESS ack moves
// the deadline out to start + in_progress_max and stops the retries.
//
// MAV_RESULT to state: ACCEPTED -> Accepted, IN_PROGRESS -> InProgress,
// CANCELLED -> Cancelled, FAILED and anything unknown -> Failed, every other
// refusal (TEMPORARILY_REJECTED, DENIED, UNSUPPORTED, COMMAND_LONG_ONLY,
// COMMAND_INT_ONLY, UNSUPPORTED_MAV_FRAME) -> Denied.
class CommandTransaction {
 public:
  CommandTransaction(CommandSpec spec, MonoTime start,
                     std::chrono::milliseconds deadline =
                         std::chrono::milliseconds{1000},
                     std::chrono::milliseconds in_progress_max =
                         std::chrono::seconds{10});

  std::optional<SendRequest> poll(MonoTime now);
  // Acks for another command, or addressed to another system, are ignored.
  void on_ack(const mavlink_command_ack_t& ack, uint8_t own_sysid,
              MonoTime now);

  TxState state() const { return state_; }
  bool terminal() const;
  std::optional<uint8_t> mav_result() const { return result_; }
  // The progress of the last IN_PROGRESS ack; unset if there was none or it
  // said 255 (unknown).
  std::optional<uint8_t> progress() const { return progress_; }
  // How many times the command has gone out, retries included.
  uint8_t sends() const { return sent_; }

 private:
  CommandSpec spec_;
  MonoTime start_;
  MonoTime deadline_at_;
  std::chrono::milliseconds in_progress_max_;
  TxState state_{TxState::Pending};
  uint8_t sent_{0};  // how many times the command has gone out
  std::optional<uint8_t> result_;
  std::optional<uint8_t> progress_;
};

}  // namespace skyrelay::mav
