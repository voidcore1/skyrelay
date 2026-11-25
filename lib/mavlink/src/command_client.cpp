#include "skyrelay/vehicle/command_client.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <stdexcept>
#include <utility>

namespace skyrelay::vehicle {

namespace {

std::vector<uint16_t> commands_of(const CommandPlan& plan) {
  std::vector<uint16_t> c;
  for (const auto& s : plan.steps) c.push_back(s.command);
  if (plan.fallback) c.push_back(plan.fallback->command);
  return c;
}

std::optional<Rejection> check_plan(const CommandRequestData& req,
                                    const CommandPlan& plan) {
  const auto unsupported = [](std::string detail) {
    return Rejection{CommandState::RejectedUnsupported, std::move(detail)};
  };
  if (plan.mission != MissionOp::None) {
    return unsupported(std::format("{} is a mission transfer, not a command",
                                   to_string(req.kind)));
  }
  if (plan.steps.empty()) {
    return unsupported(std::format("{} has no MAVLink command to send",
                                   to_string(req.kind)));
  }
  // Acks carry only the command id, so one request cannot tell two of its own
  // commands with the same id apart either.
  auto cmds = commands_of(plan);
  std::sort(cmds.begin(), cmds.end());
  if (auto dup = std::adjacent_find(cmds.begin(), cmds.end());
      dup != cmds.end()) {
    return unsupported(std::format("the plan for {} sends MAV_CMD {} twice",
                                   to_string(req.kind), *dup));
  }
  return std::nullopt;
}

CommandState state_of(mav::TxState s) {
  switch (s) {
    case mav::TxState::Accepted: return CommandState::Accepted;
    case mav::TxState::Denied: return CommandState::Denied;
    case mav::TxState::Failed: return CommandState::Failed;
    case mav::TxState::TimedOut: return CommandState::TimedOut;
    case mav::TxState::Cancelled: return CommandState::Cancelled;
    case mav::TxState::InProgress: return CommandState::InProgress;
    case mav::TxState::Pending:
    case mav::TxState::Sent: return CommandState::Sent;
  }
  return CommandState::Unknown;
}

uint8_t saturate(unsigned n) {
  return static_cast<uint8_t>(
      std::min<unsigned>(n, std::numeric_limits<uint8_t>::max()));
}

}  // namespace

CommandClient::CommandClient(CommandTiming timing) : timing_(timing) {
  if (timing.default_deadline.count() <= 0 ||
      timing.in_progress_max.count() <= 0 ||
      timing.duplicate_ack_quarantine.count() <= 0) {
    throw std::invalid_argument("command timing values must be positive");
  }
}

const mav::CommandSpec& CommandClient::current(const Request& r) const {
  return r.on_fallback ? *r.fallback : r.steps[r.step];
}

bool CommandClient::claimed(uint16_t mav_cmd, MonoTime now) const {
  return busy_reason(mav_cmd, now).has_value();
}

std::optional<std::string> CommandClient::busy_reason(uint16_t mav_cmd,
                                                      MonoTime now) const {
  for (const auto& r : requests_) {
    const bool uses =
        std::any_of(r.steps.begin(), r.steps.end(),
                    [&](const auto& s) { return s.command == mav_cmd; }) ||
        (r.fallback && r.fallback->command == mav_cmd);
    if (uses) {
      return std::format("MAV_CMD {} is in flight for another request",
                         mav_cmd);
    }
  }
  if (auto q = quarantine_.find(mav_cmd);
      q != quarantine_.end() && now < q->second.until) {
    return std::format("MAV_CMD {} {}; waiting {} ms for late acks", mav_cmd,
                       q->second.why == Hold::NoAck ? "ended without an ack"
                                                    : "was retried",
                       timing_.duplicate_ack_quarantine.count());
  }
  return std::nullopt;
}

void CommandClient::submit(const CommandRequestData& req,
                           const CommandPlan& plan, MonoTime now,
                           uint64_t rt_ns, Outbox& out) {
  if (auto bad = check_plan(req, plan)) {
    reject(req, *bad, rt_ns, out);
    return;
  }
  for (uint16_t cmd : commands_of(plan)) {
    if (auto why = busy_reason(cmd, now)) {
      reject(req, {CommandState::RejectedBusy, std::move(*why)}, rt_ns, out);
      return;
    }
  }

  Request r;
  r.key = req.key;
  r.kind = req.kind;
  r.steps = plan.steps;
  r.fallback = plan.fallback;
  r.deadline = req.deadline_ms == 0
                   ? timing_.default_deadline
                   : std::chrono::milliseconds{req.deadline_ms};
  r.t_received_ns = rt_ns;
  out.statuses.push_back(status(r, CommandState::Received));

  // The deadline is positive, so the first poll always sends.
  start(r, r.steps.front(), now, out);
  r.t_sent_ns = rt_ns;
  out.statuses.push_back(status(r, CommandState::Sent));
  requests_.push_back(std::move(r));
  forget_done(now);
}

void CommandClient::on_ack(const mavlink_command_ack_t& ack, uint8_t own_sysid,
                           MonoTime now, uint64_t rt_ns, Outbox& out) {
  // Claims are exclusive, so at most one request waits for this command.
  for (auto& r : requests_) {
    if (current(r).command != ack.command) continue;
    r.tx->on_ack(ack, own_sysid, now);
    if (r.tx->state() == mav::TxState::InProgress) r.step_in_progress = true;
    settle(r, now, rt_ns, out);
    break;
  }
  forget_done(now);
}

void CommandClient::poll(MonoTime now, uint64_t rt_ns, Outbox& out) {
  for (auto& r : requests_) {
    if (auto send = r.tx->poll(now)) out.commands.push_back(*send);
    settle(r, now, rt_ns, out);
  }
  forget_done(now);
}

void CommandClient::cancel_all(std::string_view detail, MonoTime now,
                               uint64_t rt_ns, Outbox& out) {
  for (auto& r : requests_) {
    // The vehicle may still answer what was sent.
    quarantine(current(r).command, Hold::NoAck, now);
    finish(r, CommandState::Cancelled, detail, rt_ns, out);
  }
  forget_done(now);
}

void CommandClient::start(Request& r, const mav::CommandSpec& spec,
                          MonoTime now, Outbox& out) {
  // IN_PROGRESS extends the deadline; it must not cut a longer one short.
  r.tx.emplace(spec, now, r.deadline,
               std::max(r.deadline, timing_.in_progress_max));
  r.step_in_progress = false;
  if (auto send = r.tx->poll(now)) out.commands.push_back(*send);
}

void CommandClient::settle(Request& r, MonoTime now, uint64_t rt_ns,
                           Outbox& out) {
  if (r.done) return;  // its terminal status is already out
  while (r.tx->terminal()) {
    const mav::CommandTransaction& tx = *r.tx;
    const uint16_t cmd = current(r).command;
    // A step that timed out may still be answered, and a step that went out
    // more than once may be answered once per copy.
    if (tx.state() == mav::TxState::TimedOut) {
      quarantine(cmd, Hold::NoAck, now);
    } else if (tx.sends() > 1) {
      quarantine(cmd, Hold::Retried, now);
    }

    const bool more_steps = !r.on_fallback && r.step + 1 < r.steps.size();
    const bool last_step = !r.on_fallback && r.step + 1 == r.steps.size();
    switch (tx.state()) {
      case mav::TxState::Accepted:
        if (more_steps) {
          r.earlier_sends += tx.sends();
          ++r.step;
          start(r, r.steps[r.step], now, out);
          continue;
        }
        finish(r, CommandState::Accepted, {}, rt_ns, out);
        return;
      case mav::TxState::Denied:
      case mav::TxState::Failed:
        if (last_step && r.fallback) {
          r.note = std::format(
              "MAV_CMD {} {} (MAV_RESULT {}), sent fallback MAV_CMD {}", cmd,
              tx.state() == mav::TxState::Denied ? "denied" : "failed",
              tx.mav_result().value_or(0), r.fallback->command);
          r.earlier_sends += tx.sends();
          r.on_fallback = true;
          start(r, *r.fallback, now, out);
          continue;
        }
        finish(r, state_of(tx.state()), {}, rt_ns, out);
        return;
      case mav::TxState::TimedOut: {
        const auto waited = r.step_in_progress
                                ? std::max(r.deadline, timing_.in_progress_max)
                                : r.deadline;
        finish(r, CommandState::TimedOut,
               std::format("no COMMAND_ACK for MAV_CMD {} within {} ms", cmd,
                           waited.count()),
               rt_ns, out);
        return;
      }
      default:
        finish(r, state_of(tx.state()), {}, rt_ns, out);
        return;
    }
  }

  const auto progress = r.tx->progress();
  if (r.tx->state() == mav::TxState::InProgress &&
      (!r.progress_published || progress != r.last_progress)) {
    r.progress_published = true;
    r.last_progress = progress;
    if (!r.t_in_progress_ns) r.t_in_progress_ns = rt_ns;
    auto s = status(r, CommandState::InProgress);
    if (progress) s.progress_pct = static_cast<float>(*progress);
    out.statuses.push_back(std::move(s));
  }
}

void CommandClient::finish(Request& r, CommandState state,
                           std::string_view detail, uint64_t rt_ns,
                           Outbox& out) {
  auto s = status(r, state);
  s.t_terminal_ns = rt_ns;
  s.detail = r.note;
  if (!s.detail.empty() && !detail.empty()) s.detail += "; ";
  s.detail += detail;
  out.statuses.push_back(std::move(s));
  r.done = true;
}

CommandStatusData CommandClient::status(const Request& r,
                                        CommandState state) const {
  CommandStatusData s;
  s.key = r.key;
  s.kind = r.kind;
  s.state = state;
  s.t_received_ns = r.t_received_ns;
  s.t_sent_ns = r.t_sent_ns;
  s.t_in_progress_ns = r.t_in_progress_ns;
  if (r.tx) {
    s.mav_result = r.tx->mav_result();
    s.attempts = saturate(r.earlier_sends + r.tx->sends());
  }
  return s;
}

void CommandClient::quarantine(uint16_t mav_cmd, Hold why, MonoTime now) {
  // This never cuts a running quarantine short: while one runs, no request
  // can claim the MAV_CMD, so no step that uses it can end.
  quarantine_[mav_cmd] = {now + timing_.duplicate_ack_quarantine, why};
}

void CommandClient::forget_done(MonoTime now) {
  std::erase_if(requests_, [](const Request& r) { return r.done; });
  std::erase_if(quarantine_,
                [&](const auto& q) { return q.second.until <= now; });
}

}  // namespace skyrelay::vehicle
