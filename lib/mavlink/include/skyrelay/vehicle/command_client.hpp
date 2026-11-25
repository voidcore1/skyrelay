#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "skyrelay/common/clock.hpp"
#include "skyrelay/mav/command_transaction.hpp"
#include "skyrelay/vehicle/command_types.hpp"

namespace skyrelay::vehicle {

// Every value must be positive; CommandClient refuses anything else.
struct CommandTiming {
  // For a request whose deadline_ms is 0.
  std::chrono::milliseconds default_deadline{1000};
  // An IN_PROGRESS ack moves a command's deadline out to this long after its
  // first send, unless the request's own deadline is longer still.
  std::chrono::milliseconds in_progress_max{10000};
  // How long a MAV_CMD stays claimed after a command that may still be acked
  // has ended (see CommandClient), so that late ack is not taken for the next
  // request's.
  std::chrono::milliseconds duplicate_ack_quarantine{700};
};

// The commands of one vehicle, from the request to its terminal status. Like
// CommandTransaction it has no clock and does no I/O: time comes in through
// the arguments, and every call appends what to send and what to publish to
// an Outbox.
//
// - submit() claims every MAV_CMD of the plan, steps and fallback. If one is
//   already claimed, the request is REJECTED_BUSY at once; otherwise it
//   publishes RECEIVED and SENT and sends the first step.
// - Each step is one CommandTransaction with the request's deadline (or the
//   default), retried at 350 ms and 700 ms. An ACCEPTED step starts the next
//   one at once without publishing anything. A DENIED or FAILED last step
//   sends the fallback if the plan has one, and the final status says so.
// - IN_PROGRESS is published for an IN_PROGRESS ack, and again only when its
//   progress changes.
// - Every request ends in exactly one terminal status.
// - A step whose MAV_CMD may still be acked keeps it claimed for
//   duplicate_ack_quarantine after the step ends: one that ended without a
//   definitive ack of its own (it timed out, after IN_PROGRESS or not, or
//   was cancelled by cancel_all), and one that went out more than once, since
//   the vehicle acks every copy. A step that went out once and got its
//   definitive ack frees its MAV_CMD as soon as the request ends.
class CommandClient {
 public:
  // Throws std::invalid_argument unless every timing value is positive.
  explicit CommandClient(CommandTiming timing = {});

  // The plan comes from the vehicle's adapter. A plan with no steps, a mission
  // operation or a MAV_CMD used twice is a bug in the adapter; the request is
  // then REJECTED_UNSUPPORTED rather than left without a status.
  void submit(const CommandRequestData& req, const CommandPlan& plan,
              MonoTime now, uint64_t rt_ns, Outbox& out);
  // The ack goes to the request whose current step has its command, if any.
  // Acks addressed to another system, or that no request is waiting for, are
  // dropped.
  void on_ack(const mavlink_command_ack_t& ack, uint8_t own_sysid, MonoTime now,
              uint64_t rt_ns, Outbox& out);
  // Retries and deadlines. Call it at least every few tens of milliseconds.
  void poll(MonoTime now, uint64_t rt_ns, Outbox& out);
  // Ends every request in flight as CANCELLED with this detail.
  void cancel_all(std::string_view detail, MonoTime now, uint64_t rt_ns,
                  Outbox& out);

  // True if a request in flight uses this MAV_CMD, or it is still quarantined.
  bool claimed(uint16_t mav_cmd, MonoTime now) const;
  std::size_t in_flight() const { return requests_.size(); }

 private:
  struct Request {
    CommandKey key;
    CommandKind kind = CommandKind::Unknown;
    std::vector<mav::CommandSpec> steps;
    std::optional<mav::CommandSpec> fallback;
    std::size_t step = 0;
    bool on_fallback = false;
    std::chrono::milliseconds deadline{0};
    std::optional<mav::CommandTransaction> tx;
    // The current step has had an IN_PROGRESS ack.
    bool step_in_progress = false;
    // Sends of the steps before the current one.
    unsigned earlier_sends = 0;
    bool progress_published = false;
    std::optional<uint8_t> last_progress;
    uint64_t t_received_ns = 0;
    std::optional<uint64_t> t_sent_ns;
    std::optional<uint64_t> t_in_progress_ns;
    // Why the fallback went out; it leads the final detail.
    std::string note;
    bool done = false;
  };

  enum class Hold : uint8_t { Retried, NoAck };
  struct Quarantine {
    MonoTime until;
    Hold why;
  };

  std::optional<std::string> busy_reason(uint16_t mav_cmd, MonoTime now) const;
  void start(Request& r, const mav::CommandSpec& spec, MonoTime now,
             Outbox& out);
  // Moves a request on after its transaction changed: the next step, the
  // fallback, a terminal status or an IN_PROGRESS status.
  void settle(Request& r, MonoTime now, uint64_t rt_ns, Outbox& out);
  void finish(Request& r, CommandState state, std::string_view detail,
              uint64_t rt_ns, Outbox& out);
  CommandStatusData status(const Request& r, CommandState state) const;
  const mav::CommandSpec& current(const Request& r) const;
  void quarantine(uint16_t mav_cmd, Hold why, MonoTime now);
  void forget_done(MonoTime now);

  CommandTiming timing_;
  std::vector<Request> requests_;
  std::map<uint16_t, Quarantine> quarantine_;
};

}  // namespace skyrelay::vehicle
