#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "skyrelay/common/clock.hpp"
#include "skyrelay/mav/command_transaction.hpp"

namespace skyrelay::mav {

struct IntervalRequest {
  uint32_t msg_id;
  double hz;
};

// What to ask a vehicle for when it (re)connects: AUTOPILOT_VERSION first,
// then one SET_MESSAGE_INTERVAL per requested message. Strictly sequential,
// because an ack only carries the command id, so two SET_MESSAGE_INTERVAL
// transactions in flight could not be told apart. A step that is denied,
// fails or times out is recorded and the sequence moves on.
class SetupSequence {
 public:
  SetupSequence(std::vector<IntervalRequest> intervals, uint8_t target_sys,
                uint8_t target_comp, MonoTime start);

  std::optional<SendRequest> poll(MonoTime now);
  void on_ack(const mavlink_command_ack_t& ack, uint8_t own_sysid,
              MonoTime now);

  bool done() const { return !current_ && next_ >= steps_.size(); }
  std::vector<uint32_t> failed_msg_ids() const { return failed_; }

 private:
  struct Step {
    CommandSpec spec;
    uint32_t msg_id;
  };

  void start_next(MonoTime now);
  void finish_if_terminal(MonoTime now);

  std::vector<Step> steps_;
  std::size_t next_{0};
  std::optional<CommandTransaction> current_;
  uint32_t current_msg_id_{0};
  std::vector<uint32_t> failed_;
};

}  // namespace skyrelay::mav
