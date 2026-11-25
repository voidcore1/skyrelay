#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <initializer_list>
#include <optional>
#include <vector>

#include "skyrelay/vehicle/command_client.hpp"

using namespace skyrelay;
using namespace skyrelay::vehicle;
using std::chrono::milliseconds;

namespace {

constexpr uint8_t kGcs = 245;

const MonoTime t0{};
MonoTime at(int ms) { return t0 + milliseconds{ms}; }
// Realtime stamps run alongside, from an arbitrary epoch.
uint64_t rt(int ms) { return 1'700'000'000'000'000'000ull + static_cast<uint64_t>(ms) * 1'000'000; }

mav::CommandSpec spec(uint16_t command) {
  return mav::CommandSpec{command, {1, 0, 0, 0, 0, 0, 0}, 1, 1};
}

CommandPlan plan(std::initializer_list<uint16_t> commands,
                 std::optional<uint16_t> fallback = std::nullopt) {
  CommandPlan p;
  for (auto c : commands) p.steps.push_back(spec(c));
  if (fallback) p.fallback = spec(*fallback);
  return p;
}

CommandRequestData request(uint64_t id, CommandKind kind = CommandKind::Arm,
                           uint32_t deadline_ms = 0) {
  CommandRequestData r;
  r.key = {2, 1, id};
  r.kind = kind;
  r.deadline_ms = deadline_ms;
  return r;
}

mavlink_command_ack_t ack(uint16_t command, uint8_t result, uint8_t progress = 0,
                          uint8_t target = kGcs) {
  mavlink_command_ack_t a{};
  a.command = command;
  a.result = result;
  a.progress = progress;
  a.target_system = target;
  return a;
}

std::vector<CommandState> states(const Outbox& out) {
  std::vector<CommandState> s;
  for (const auto& st : out.statuses) s.push_back(st.state);
  return s;
}

using S = CommandState;

}  // namespace

TEST(CommandClient, SubmitPublishesReceivedThenSent) {
  CommandClient c;
  Outbox out;
  c.submit(request(7), plan({400}), at(0), rt(0), out);

  ASSERT_EQ(out.commands.size(), 1u);
  EXPECT_EQ(out.commands[0].spec.command, 400);
  EXPECT_EQ(out.commands[0].confirmation, 0);
  ASSERT_EQ(states(out), (std::vector<S>{S::Received, S::Sent}));

  const auto& received = out.statuses[0];
  EXPECT_EQ(received.key, (CommandKey{2, 1, 7}));
  EXPECT_EQ(received.kind, CommandKind::Arm);
  EXPECT_EQ(received.t_received_ns, rt(0));
  EXPECT_FALSE(received.t_sent_ns);
  EXPECT_FALSE(received.t_terminal_ns);
  EXPECT_EQ(received.attempts, 0);

  const auto& sent = out.statuses[1];
  EXPECT_EQ(sent.key, (CommandKey{2, 1, 7}));
  EXPECT_EQ(sent.t_received_ns, rt(0));
  EXPECT_EQ(sent.t_sent_ns, rt(0));
  EXPECT_FALSE(sent.t_terminal_ns);
  EXPECT_FALSE(sent.mav_result);
  EXPECT_EQ(sent.attempts, 1);

  EXPECT_EQ(c.in_flight(), 1u);
  EXPECT_TRUE(c.claimed(400, at(0)));
  EXPECT_FALSE(c.claimed(176, at(0)));
}

TEST(CommandClient, AcceptedIsTerminal) {
  CommandClient c;
  Outbox out;
  c.submit(request(7), plan({400}), at(0), rt(0), out);
  out = {};
  c.on_ack(ack(400, MAV_RESULT_ACCEPTED), kGcs, at(50), rt(50), out);

  ASSERT_EQ(states(out), std::vector<S>{S::Accepted});
  const auto& s = out.statuses[0];
  EXPECT_EQ(s.mav_result, std::optional<uint8_t>{0});
  EXPECT_EQ(s.t_received_ns, rt(0));
  EXPECT_EQ(s.t_sent_ns, rt(0));
  EXPECT_EQ(s.t_terminal_ns, rt(50));
  EXPECT_FALSE(s.t_in_progress_ns);
  EXPECT_EQ(s.attempts, 1);
  EXPECT_TRUE(s.detail.empty());
  EXPECT_EQ(c.in_flight(), 0u);
  EXPECT_FALSE(c.claimed(400, at(50)));
}

TEST(CommandClient, RetriesThenTimesOut) {
  CommandClient c;
  Outbox out;
  c.submit(request(7), plan({400}), at(0), rt(0), out);

  out = {};
  c.poll(at(349), rt(349), out);
  EXPECT_TRUE(out.commands.empty());
  c.poll(at(350), rt(350), out);
  ASSERT_EQ(out.commands.size(), 1u);
  EXPECT_EQ(out.commands[0].confirmation, 1);
  c.poll(at(700), rt(700), out);
  ASSERT_EQ(out.commands.size(), 2u);
  EXPECT_EQ(out.commands[1].confirmation, 2);
  c.poll(at(999), rt(999), out);
  EXPECT_EQ(out.commands.size(), 2u);
  // Retries publish nothing.
  EXPECT_TRUE(out.statuses.empty());

  c.poll(at(1000), rt(1000), out);
  ASSERT_EQ(states(out), std::vector<S>{S::TimedOut});
  const auto& s = out.statuses[0];
  EXPECT_EQ(s.attempts, 3);
  EXPECT_EQ(s.detail, "no COMMAND_ACK for MAV_CMD 400 within 1000 ms");
  EXPECT_EQ(s.t_terminal_ns, rt(1000));
  EXPECT_FALSE(s.mav_result);
  EXPECT_EQ(c.in_flight(), 0u);
}

TEST(CommandClient, RequestDeadlineOverridesDefault) {
  CommandClient c;
  Outbox out;
  c.submit(request(7, CommandKind::Arm, 3000), plan({400}), at(0), rt(0), out);
  out = {};
  c.poll(at(2999), rt(2999), out);
  EXPECT_TRUE(out.statuses.empty());
  EXPECT_EQ(c.in_flight(), 1u);
  c.poll(at(3000), rt(3000), out);
  ASSERT_EQ(states(out), std::vector<S>{S::TimedOut});
  EXPECT_EQ(out.statuses[0].detail, "no COMMAND_ACK for MAV_CMD 400 within 3000 ms");
}

TEST(CommandClient, InProgressPublishesOnChangeAndExtends) {
  CommandClient c;
  Outbox out;
  c.submit(request(7), plan({400}), at(0), rt(0), out);

  out = {};
  c.on_ack(ack(400, MAV_RESULT_IN_PROGRESS, 40), kGcs, at(100), rt(100), out);
  ASSERT_EQ(states(out), std::vector<S>{S::InProgress});
  const auto first = out.statuses[0];
  EXPECT_FLOAT_EQ(first.progress_pct, 40.0f);
  EXPECT_EQ(first.mav_result, std::optional<uint8_t>{MAV_RESULT_IN_PROGRESS});
  EXPECT_EQ(first.t_received_ns, rt(0));
  EXPECT_EQ(first.t_sent_ns, rt(0));
  EXPECT_EQ(first.t_in_progress_ns, rt(100));
  EXPECT_FALSE(first.t_terminal_ns);

  out = {};
  c.on_ack(ack(400, MAV_RESULT_IN_PROGRESS, 40), kGcs, at(200), rt(200), out);
  EXPECT_TRUE(out.statuses.empty());

  c.on_ack(ack(400, MAV_RESULT_IN_PROGRESS, 255), kGcs, at(300), rt(300), out);
  ASSERT_EQ(states(out), std::vector<S>{S::InProgress});
  EXPECT_TRUE(std::isnan(out.statuses[0].progress_pct));
  // t_in_progress_ns stays the first IN_PROGRESS.
  EXPECT_EQ(out.statuses[0].t_in_progress_ns, rt(100));

  out = {};
  c.poll(at(9999), rt(9999), out);
  EXPECT_TRUE(out.statuses.empty());
  EXPECT_TRUE(out.commands.empty());  // no retries once IN_PROGRESS
  c.poll(at(10000), rt(10000), out);
  ASSERT_EQ(states(out), std::vector<S>{S::TimedOut});
  EXPECT_EQ(out.statuses[0].detail, "no COMMAND_ACK for MAV_CMD 400 within 10000 ms");
  EXPECT_EQ(out.statuses[0].t_in_progress_ns, rt(100));
  EXPECT_EQ(out.statuses[0].t_terminal_ns, rt(10000));
  EXPECT_TRUE(std::isnan(out.statuses[0].progress_pct));
}

// IN_PROGRESS extends a deadline; it never cuts a longer one short.
TEST(CommandClient, InProgressKeepsALongerRequestDeadline) {
  CommandClient c;
  Outbox out;
  c.submit(request(7, CommandKind::Arm, 30000), plan({400}), at(0), rt(0), out);
  c.on_ack(ack(400, MAV_RESULT_IN_PROGRESS, 10), kGcs, at(100), rt(100), out);
  out = {};
  c.poll(at(29999), rt(29999), out);
  EXPECT_TRUE(out.statuses.empty());
  c.poll(at(30000), rt(30000), out);
  ASSERT_EQ(states(out), std::vector<S>{S::TimedOut});
  EXPECT_EQ(out.statuses[0].detail, "no COMMAND_ACK for MAV_CMD 400 within 30000 ms");
}

TEST(CommandClient, MapsResultsToStates) {
  const std::pair<uint8_t, S> table[] = {
      {0, S::Accepted}, {1, S::Denied}, {2, S::Denied},   {3, S::Denied},
      {4, S::Failed},   {6, S::Cancelled}, {7, S::Denied}, {8, S::Denied},
      {9, S::Denied},   {200, S::Failed}};
  for (auto [result, expected] : table) {
    CommandClient c;
    Outbox out;
    c.submit(request(7), plan({400}), at(0), rt(0), out);
    out = {};
    c.on_ack(ack(400, result), kGcs, at(10), rt(10), out);
    ASSERT_EQ(states(out), std::vector<S>{expected}) << int(result);
    EXPECT_EQ(out.statuses[0].mav_result, std::optional<uint8_t>{result});
    EXPECT_EQ(out.statuses[0].t_terminal_ns, rt(10));
    EXPECT_EQ(c.in_flight(), 0u);
  }
}

TEST(CommandClient, SecondRequestSameMavCmdIsBusy) {
  CommandClient c;
  Outbox out;
  c.submit(request(1, CommandKind::Hold), plan({176}), at(0), rt(0), out);

  out = {};
  c.submit(request(2, CommandKind::MissionPause), plan({176}), at(10), rt(10), out);
  EXPECT_TRUE(out.commands.empty());
  ASSERT_EQ(states(out), (std::vector<S>{S::Received, S::RejectedBusy}));
  const auto& busy = out.statuses[1];
  EXPECT_EQ(busy.key.command_id, 2u);
  EXPECT_EQ(busy.kind, CommandKind::MissionPause);
  EXPECT_EQ(busy.detail, "MAV_CMD 176 is in flight for another request");
  EXPECT_EQ(busy.t_received_ns, rt(10));
  EXPECT_EQ(busy.t_terminal_ns, rt(10));
  EXPECT_EQ(busy.attempts, 0);

  // A later step or the fallback counts as well.
  out = {};
  c.submit(request(3), plan({400, 176}), at(20), rt(20), out);
  EXPECT_EQ(states(out), (std::vector<S>{S::Received, S::RejectedBusy}));
  out = {};
  c.submit(request(4), plan({20}, 176), at(20), rt(20), out);
  EXPECT_EQ(states(out), (std::vector<S>{S::Received, S::RejectedBusy}));
  EXPECT_TRUE(out.commands.empty());

  EXPECT_EQ(c.in_flight(), 1u);
  out = {};
  c.on_ack(ack(176, MAV_RESULT_ACCEPTED), kGcs, at(30), rt(30), out);
  ASSERT_EQ(states(out), std::vector<S>{S::Accepted});
  EXPECT_EQ(out.statuses[0].key.command_id, 1u);
}

TEST(CommandClient, DifferentMavCmdsOverlap) {
  CommandClient c;
  Outbox out;
  c.submit(request(1), plan({400}), at(0), rt(0), out);
  c.submit(request(2, CommandKind::SetSpeed), plan({178}), at(5), rt(5), out);
  EXPECT_EQ(out.commands.size(), 2u);
  EXPECT_EQ(c.in_flight(), 2u);

  out = {};
  c.on_ack(ack(178, MAV_RESULT_ACCEPTED), kGcs, at(20), rt(20), out);
  ASSERT_EQ(states(out), std::vector<S>{S::Accepted});
  EXPECT_EQ(out.statuses[0].key.command_id, 2u);

  out = {};
  c.on_ack(ack(400, MAV_RESULT_DENIED), kGcs, at(30), rt(30), out);
  ASSERT_EQ(states(out), std::vector<S>{S::Denied});
  EXPECT_EQ(out.statuses[0].key.command_id, 1u);
  EXPECT_EQ(c.in_flight(), 0u);
}

TEST(CommandClient, MultiStepPlanAdvancesOnAccept) {
  CommandClient c;
  Outbox out;
  c.submit(request(7, CommandKind::Takeoff), plan({176, 22}), at(0), rt(0), out);
  ASSERT_EQ(out.commands.size(), 1u);
  EXPECT_EQ(out.commands[0].spec.command, 176);

  out = {};
  c.on_ack(ack(176, MAV_RESULT_ACCEPTED), kGcs, at(100), rt(100), out);
  ASSERT_EQ(out.commands.size(), 1u);
  EXPECT_EQ(out.commands[0].spec.command, 22);
  EXPECT_EQ(out.commands[0].confirmation, 0);
  EXPECT_TRUE(out.statuses.empty());
  // The request holds every MAV_CMD of its plan until it ends.
  EXPECT_TRUE(c.claimed(176, at(100)));
  EXPECT_TRUE(c.claimed(22, at(100)));

  // The next step has its own deadline, counted from its first send.
  out = {};
  c.poll(at(1050), rt(1050), out);
  EXPECT_TRUE(out.statuses.empty());

  c.on_ack(ack(22, MAV_RESULT_ACCEPTED), kGcs, at(1060), rt(1060), out);
  ASSERT_EQ(states(out), std::vector<S>{S::Accepted});
  EXPECT_EQ(out.statuses[0].attempts, 3);  // 176 once, 22 twice
  EXPECT_EQ(out.statuses[0].t_terminal_ns, rt(1060));
  EXPECT_EQ(c.in_flight(), 0u);
}

TEST(CommandClient, MultiStepStopsOnDeny) {
  CommandClient c;
  Outbox out;
  c.submit(request(7, CommandKind::Takeoff), plan({176, 22}), at(0), rt(0), out);
  out = {};
  c.on_ack(ack(176, MAV_RESULT_DENIED), kGcs, at(100), rt(100), out);
  ASSERT_EQ(states(out), std::vector<S>{S::Denied});
  EXPECT_EQ(out.statuses[0].mav_result, std::optional<uint8_t>{MAV_RESULT_DENIED});
  EXPECT_EQ(out.statuses[0].attempts, 1);
  EXPECT_TRUE(out.commands.empty());
  EXPECT_EQ(c.in_flight(), 0u);

  out = {};
  c.poll(at(2000), rt(2000), out);
  EXPECT_TRUE(out.commands.empty());
  EXPECT_TRUE(out.statuses.empty());
}

TEST(CommandClient, FallbackAfterDeny) {
  CommandClient c;
  Outbox out;
  c.submit(request(7, CommandKind::EmergencyAbort), plan({20}, 21), at(0), rt(0), out);
  out = {};
  c.on_ack(ack(20, MAV_RESULT_DENIED), kGcs, at(100), rt(100), out);
  ASSERT_EQ(out.commands.size(), 1u);
  EXPECT_EQ(out.commands[0].spec.command, 21);
  EXPECT_TRUE(out.statuses.empty());

  out = {};
  c.on_ack(ack(21, MAV_RESULT_ACCEPTED), kGcs, at(200), rt(200), out);
  ASSERT_EQ(states(out), std::vector<S>{S::Accepted});
  EXPECT_EQ(out.statuses[0].detail, "MAV_CMD 20 denied (MAV_RESULT 2), sent fallback MAV_CMD 21");
  EXPECT_EQ(out.statuses[0].mav_result, std::optional<uint8_t>{0});
  EXPECT_EQ(out.statuses[0].attempts, 2);
}

TEST(CommandClient, FallbackOnlyAfterDenyOrFailure) {
  {  // FAILED also sends the fallback, and a timeout of the fallback keeps the note.
    CommandClient c;
    Outbox out;
    c.submit(request(7, CommandKind::EmergencyAbort), plan({20}, 21), at(0), rt(0), out);
    c.on_ack(ack(20, MAV_RESULT_FAILED), kGcs, at(100), rt(100), out);
    c.poll(at(450), rt(450), out);
    c.poll(at(800), rt(800), out);
    out = {};
    c.poll(at(1099), rt(1099), out);
    EXPECT_TRUE(out.statuses.empty());
    c.poll(at(1100), rt(1100), out);
    ASSERT_EQ(states(out), std::vector<S>{S::TimedOut});
    EXPECT_EQ(out.statuses[0].detail,
              "MAV_CMD 20 failed (MAV_RESULT 4), sent fallback MAV_CMD 21; "
              "no COMMAND_ACK for MAV_CMD 21 within 1000 ms");
    EXPECT_FALSE(out.statuses[0].mav_result);
    EXPECT_EQ(out.statuses[0].attempts, 4);
  }
  {  // A timeout of the last step is not an answer, so no fallback.
    CommandClient c;
    Outbox out;
    c.submit(request(7, CommandKind::EmergencyAbort), plan({20}, 21), at(0), rt(0), out);
    out = {};
    c.poll(at(1000), rt(1000), out);
    ASSERT_EQ(states(out), std::vector<S>{S::TimedOut});
    for (const auto& s : out.commands) EXPECT_NE(s.spec.command, 21);
  }
  {  // Nor is a denial of an earlier step: the fallback follows the last one.
    CommandClient c;
    Outbox out;
    c.submit(request(7), plan({176, 20}, 21), at(0), rt(0), out);
    out = {};
    c.on_ack(ack(176, MAV_RESULT_DENIED), kGcs, at(100), rt(100), out);
    ASSERT_EQ(states(out), std::vector<S>{S::Denied});
    EXPECT_TRUE(out.commands.empty());
  }
}

// A retried command can be acked once per copy. Its MAV_CMD stays claimed for
// a while after it ends, so a late ack is not credited to the next request.
TEST(CommandClient, RetriedCommandQuarantinesItsMavCmd) {
  CommandClient c;
  Outbox out;
  c.submit(request(1, CommandKind::Hold), plan({176}), at(0), rt(0), out);
  c.poll(at(350), rt(350), out);
  ASSERT_EQ(out.commands.size(), 2u);
  out = {};
  c.on_ack(ack(176, MAV_RESULT_ACCEPTED), kGcs, at(400), rt(400), out);
  ASSERT_EQ(states(out), std::vector<S>{S::Accepted});
  EXPECT_EQ(out.statuses[0].attempts, 2);
  EXPECT_EQ(c.in_flight(), 0u);

  out = {};
  c.submit(request(2, CommandKind::MissionPause), plan({176}), at(500), rt(500), out);
  ASSERT_EQ(states(out), (std::vector<S>{S::Received, S::RejectedBusy}));
  EXPECT_EQ(out.statuses[1].detail, "MAV_CMD 176 was retried; waiting 700 ms for late acks");
  EXPECT_TRUE(out.commands.empty());

  out = {};
  c.on_ack(ack(176, MAV_RESULT_ACCEPTED), kGcs, at(600), rt(600), out);
  EXPECT_TRUE(out.statuses.empty());

  EXPECT_TRUE(c.claimed(176, at(1099)));
  EXPECT_FALSE(c.claimed(176, at(1100)));
  c.submit(request(3, CommandKind::MissionPause), plan({176}), at(1100), rt(1100), out);
  ASSERT_EQ(states(out), (std::vector<S>{S::Received, S::Sent}));

  // Control: a command that went out once is free again as soon as it ends.
  CommandClient once;
  Outbox o;
  once.submit(request(1, CommandKind::Hold), plan({176}), at(0), rt(0), o);
  once.on_ack(ack(176, MAV_RESULT_ACCEPTED), kGcs, at(100), rt(100), o);
  EXPECT_FALSE(once.claimed(176, at(100)));
  o = {};
  once.submit(request(2, CommandKind::MissionPause), plan({176}), at(101), rt(101), o);
  ASSERT_EQ(states(o), (std::vector<S>{S::Received, S::Sent}));
}

TEST(CommandClient, DuplicateAckAfterTerminalIsIgnored) {
  CommandClient c;
  Outbox out;
  c.submit(request(7), plan({400}), at(0), rt(0), out);
  c.on_ack(ack(400, MAV_RESULT_ACCEPTED), kGcs, at(50), rt(50), out);
  out = {};
  c.on_ack(ack(400, MAV_RESULT_ACCEPTED), kGcs, at(60), rt(60), out);
  c.on_ack(ack(400, MAV_RESULT_DENIED), kGcs, at(70), rt(70), out);
  EXPECT_TRUE(out.statuses.empty());
  EXPECT_TRUE(out.commands.empty());
}

TEST(CommandClient, AckForAnotherSystemIsIgnored) {
  CommandClient c;
  Outbox out;
  c.submit(request(7), plan({400}), at(0), rt(0), out);
  out = {};
  c.on_ack(ack(400, MAV_RESULT_ACCEPTED, 0, 200), kGcs, at(50), rt(50), out);
  c.on_ack(ack(400, MAV_RESULT_IN_PROGRESS, 10, 200), kGcs, at(60), rt(60), out);
  EXPECT_TRUE(out.statuses.empty());
  EXPECT_EQ(c.in_flight(), 1u);
  // An ack for another command leaves this one alone too.
  c.on_ack(ack(176, MAV_RESULT_ACCEPTED), kGcs, at(70), rt(70), out);
  EXPECT_TRUE(out.statuses.empty());

  c.on_ack(ack(400, MAV_RESULT_ACCEPTED), kGcs, at(80), rt(80), out);
  EXPECT_EQ(states(out), std::vector<S>{S::Accepted});
}

TEST(CommandClient, CancelAllCancelsInFlight) {
  CommandClient c;
  Outbox out;
  c.submit(request(1), plan({400}), at(0), rt(0), out);
  c.submit(request(2, CommandKind::Hold), plan({176}), at(0), rt(0), out);
  c.on_ack(ack(176, MAV_RESULT_IN_PROGRESS, 50), kGcs, at(10), rt(10), out);

  out = {};
  c.cancel_all("bridge shutting down", at(20), rt(20), out);
  ASSERT_EQ(states(out), (std::vector<S>{S::Cancelled, S::Cancelled}));
  for (const auto& s : out.statuses) {
    EXPECT_EQ(s.detail, "bridge shutting down");
    EXPECT_EQ(s.t_terminal_ns, rt(20));
    EXPECT_EQ(s.attempts, 1);
  }
  EXPECT_EQ(c.in_flight(), 0u);
  // The vehicle may still answer what was sent, so both stay claimed for a
  // while even though each went out once.
  EXPECT_TRUE(c.claimed(400, at(719)));
  EXPECT_TRUE(c.claimed(176, at(719)));
  EXPECT_FALSE(c.claimed(400, at(720)));
  out = {};
  c.submit(request(3), plan({400}), at(30), rt(30), out);
  ASSERT_EQ(states(out), (std::vector<S>{S::Received, S::RejectedBusy}));
  EXPECT_EQ(out.statuses[1].detail,
            "MAV_CMD 400 ended without an ack; waiting 700 ms for late acks");

  out = {};
  c.poll(at(2000), rt(2000), out);
  c.on_ack(ack(400, MAV_RESULT_ACCEPTED), kGcs, at(2000), rt(2000), out);
  EXPECT_TRUE(out.statuses.empty());
  EXPECT_TRUE(out.commands.empty());
}

TEST(CommandClient, MalformedPlanIsRejected) {
  CommandClient c;
  Outbox out;
  c.submit(request(1), CommandPlan{}, at(0), rt(0), out);
  ASSERT_EQ(states(out), (std::vector<S>{S::Received, S::RejectedUnsupported}));
  EXPECT_EQ(out.statuses[1].detail, "ARM has no MAVLink command to send");

  out = {};
  CommandPlan mission;
  mission.mission = MissionOp::Upload;
  c.submit(request(2, CommandKind::MissionUpload), mission, at(0), rt(0), out);
  ASSERT_EQ(states(out), (std::vector<S>{S::Received, S::RejectedUnsupported}));
  EXPECT_EQ(out.statuses[1].detail, "MISSION_UPLOAD is a mission transfer, not a command");

  out = {};
  c.submit(request(3), plan({176, 176}), at(0), rt(0), out);
  ASSERT_EQ(states(out), (std::vector<S>{S::Received, S::RejectedUnsupported}));
  EXPECT_EQ(out.statuses[1].detail, "the plan for ARM sends MAV_CMD 176 twice");

  EXPECT_TRUE(out.commands.empty());
  EXPECT_EQ(c.in_flight(), 0u);
}

// A command that timed out after one send can still be answered late. Its
// MAV_CMD stays claimed, so the late ack is not credited to the next request.
TEST(CommandClient, SingleSendTimeoutQuarantines) {
  CommandClient c;
  Outbox out;
  c.submit(request(1, CommandKind::Goto, 300), plan({192}), at(0), rt(0), out);
  out = {};
  c.poll(at(300), rt(300), out);
  ASSERT_EQ(states(out), std::vector<S>{S::TimedOut});
  EXPECT_EQ(out.statuses[0].attempts, 1);

  out = {};
  c.submit(request(2, CommandKind::Goto, 300), plan({192}), at(320), rt(320), out);
  ASSERT_EQ(states(out), (std::vector<S>{S::Received, S::RejectedBusy}));
  const std::string& detail = out.statuses[1].detail;
  EXPECT_EQ(detail, "MAV_CMD 192 ended without an ack; waiting 700 ms for late acks");
  EXPECT_LE(detail.size(), 128u);
  EXPECT_TRUE(out.commands.empty());

  out = {};
  c.on_ack(ack(192, MAV_RESULT_ACCEPTED), kGcs, at(360), rt(360), out);
  EXPECT_TRUE(out.statuses.empty());

  EXPECT_TRUE(c.claimed(192, at(999)));
  EXPECT_FALSE(c.claimed(192, at(1000)));
  c.submit(request(3, CommandKind::Goto), plan({192}), at(1000), rt(1000), out);
  EXPECT_EQ(states(out), (std::vector<S>{S::Received, S::Sent}));
}

// IN_PROGRESS promises a final ack. If the deadline passes first, that ack is
// still owed, and must not end the next request for the same MAV_CMD.
TEST(CommandClient, InProgressThenTimeoutQuarantines) {
  CommandClient c;
  Outbox out;
  c.submit(request(1, CommandKind::Hold), plan({176}), at(0), rt(0), out);
  c.on_ack(ack(176, MAV_RESULT_IN_PROGRESS, 255), kGcs, at(100), rt(100), out);
  out = {};
  c.poll(at(10000), rt(10000), out);
  ASSERT_EQ(states(out), std::vector<S>{S::TimedOut});
  EXPECT_EQ(out.statuses[0].attempts, 1);

  out = {};
  c.submit(request(2, CommandKind::MissionStart), plan({176}), at(10010), rt(10010), out);
  ASSERT_EQ(states(out), (std::vector<S>{S::Received, S::RejectedBusy}));
  EXPECT_EQ(out.statuses[1].detail,
            "MAV_CMD 176 ended without an ack; waiting 700 ms for late acks");

  out = {};
  c.on_ack(ack(176, MAV_RESULT_DENIED), kGcs, at(10050), rt(10050), out);
  EXPECT_TRUE(out.statuses.empty());

  c.submit(request(3, CommandKind::MissionStart), plan({176}), at(10700), rt(10700), out);
  EXPECT_EQ(states(out), (std::vector<S>{S::Received, S::Sent}));
}

TEST(CommandClient, RetriedDenialQuarantines) {
  CommandClient c;
  Outbox out;
  c.submit(request(1, CommandKind::Hold), plan({176}), at(0), rt(0), out);
  c.poll(at(350), rt(350), out);
  out = {};
  c.on_ack(ack(176, MAV_RESULT_DENIED), kGcs, at(400), rt(400), out);
  ASSERT_EQ(states(out), std::vector<S>{S::Denied});
  EXPECT_EQ(out.statuses[0].attempts, 2);

  out = {};
  c.submit(request(2, CommandKind::Hold), plan({176}), at(1099), rt(1099), out);
  ASSERT_EQ(states(out), (std::vector<S>{S::Received, S::RejectedBusy}));
  EXPECT_EQ(out.statuses[1].detail, "MAV_CMD 176 was retried; waiting 700 ms for late acks");
  EXPECT_FALSE(c.claimed(176, at(1100)));
}

// The quarantine starts when the step ends, not when the request does.
TEST(CommandClient, EarlierStepQuarantines) {
  CommandClient c;
  Outbox out;
  c.submit(request(1, CommandKind::Takeoff), plan({176, 22}), at(0), rt(0), out);
  c.poll(at(350), rt(350), out);  // 176 again
  c.on_ack(ack(176, MAV_RESULT_ACCEPTED), kGcs, at(400), rt(400), out);
  out = {};
  c.on_ack(ack(22, MAV_RESULT_ACCEPTED), kGcs, at(450), rt(450), out);
  ASSERT_EQ(states(out), std::vector<S>{S::Accepted});
  EXPECT_EQ(out.statuses[0].attempts, 3);
  EXPECT_EQ(c.in_flight(), 0u);

  // 22 went out once and was answered: free at once. 176 was retried.
  EXPECT_FALSE(c.claimed(22, at(450)));
  EXPECT_TRUE(c.claimed(176, at(1099)));
  EXPECT_FALSE(c.claimed(176, at(1100)));
  out = {};
  c.submit(request(2, CommandKind::Hold), plan({176}), at(500), rt(500), out);
  ASSERT_EQ(states(out), (std::vector<S>{S::Received, S::RejectedBusy}));
  EXPECT_EQ(out.statuses[1].detail, "MAV_CMD 176 was retried; waiting 700 ms for late acks");
}

TEST(CommandClient, FallbackDeniedToo) {
  CommandClient c;
  Outbox out;
  c.submit(request(7, CommandKind::EmergencyAbort), plan({20}, 21), at(0), rt(0), out);
  c.on_ack(ack(20, MAV_RESULT_DENIED), kGcs, at(100), rt(100), out);
  out = {};
  c.on_ack(ack(21, MAV_RESULT_TEMPORARILY_REJECTED), kGcs, at(200), rt(200), out);
  ASSERT_EQ(states(out), std::vector<S>{S::Denied});
  const auto& s = out.statuses[0];
  EXPECT_EQ(s.detail, "MAV_CMD 20 denied (MAV_RESULT 2), sent fallback MAV_CMD 21");
  EXPECT_EQ(s.mav_result, std::optional<uint8_t>{MAV_RESULT_TEMPORARILY_REJECTED});
  EXPECT_EQ(s.attempts, 2);
  EXPECT_EQ(s.t_terminal_ns, rt(200));
  EXPECT_TRUE(out.commands.empty());
  EXPECT_EQ(c.in_flight(), 0u);

  out = {};
  c.poll(at(3000), rt(3000), out);
  EXPECT_TRUE(out.commands.empty());
  EXPECT_TRUE(out.statuses.empty());
}

TEST(CommandClient, ProgressAbove100IsUnknown) {
  CommandClient c;
  Outbox out;
  c.submit(request(7), plan({400}), at(0), rt(0), out);
  out = {};
  c.on_ack(ack(400, MAV_RESULT_IN_PROGRESS, 150), kGcs, at(100), rt(100), out);
  ASSERT_EQ(states(out), std::vector<S>{S::InProgress});
  EXPECT_TRUE(std::isnan(out.statuses[0].progress_pct));
}

TEST(CommandClient, TimingMustBePositive) {
  using std::chrono::milliseconds;
  EXPECT_NO_THROW(CommandClient{});
  for (auto bad : {milliseconds{0}, milliseconds{-1}}) {
    CommandTiming t;
    t.default_deadline = bad;
    EXPECT_THROW(CommandClient{t}, std::invalid_argument);
    t = {};
    t.in_progress_max = bad;
    EXPECT_THROW(CommandClient{t}, std::invalid_argument);
    t = {};
    t.duplicate_ack_quarantine = bad;
    EXPECT_THROW(CommandClient{t}, std::invalid_argument);
  }
}
