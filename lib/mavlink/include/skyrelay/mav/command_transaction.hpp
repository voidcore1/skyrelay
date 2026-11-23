#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

#include "skyrelay/common/clock.hpp"
#include "skyrelay/mav/mavlink.hpp"

namespace skyrelay::mav {

struct CommandSpec {
  uint16_t command;
  std::array<float, 7> params{};
  uint8_t target_sys;
  uint8_t target_comp;
};

enum class TxState : uint8_t {
  Pending,
  Sent,
  InProgress,
  Accepted,
  Denied,
  Failed,
  TimedOut
};

// What to put on the wire; the caller packs it with Packer::command_long.
struct SendRequest {
  CommandSpec spec;
  uint8_t confirmation;
};

// One COMMAND_LONG with retries, matched against its COMMAND_ACK. It never
// reads a clock or does I/O: time comes in through the arguments and poll()
// says what to send. Retries go out at 350 ms and 700 ms with confirmation 1
// and 2; the transaction times out at the deadline. An IN_PROGRESS ack moves
// the deadline out to start + in_progress_max and stops the retries.
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

 private:
  CommandSpec spec_;
  MonoTime start_;
  MonoTime deadline_at_;
  std::chrono::milliseconds in_progress_max_;
  TxState state_{TxState::Pending};
  uint8_t sent_{0};  // how many times the command has gone out
  std::optional<uint8_t> result_;
};

}  // namespace skyrelay::mav
