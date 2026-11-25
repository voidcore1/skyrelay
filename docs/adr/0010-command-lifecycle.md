# 0010. Command lifecycle: plans, claims and the duplicate-ack quarantine

- Status: Accepted
- Date: 2026-11-25

## Context

ADR-0009 fixed what a command looks like on the bus and which states it
passes through. This one is about how the bridge gets each request from
`RECEIVED` to exactly one terminal state, given what MAVLink offers:

- A `COMMAND_ACK` names only the `MAV_CMD` it answers, not which copy or
  which request. Two commands with the same id in flight cannot be told apart.
- Autopilots ack every copy they receive. A command resent at 350 ms and
  700 ms can be acked up to three times, and the later acks can arrive after
  the bridge has already finished with the command.
- Several kinds share a `MAV_CMD`. On PX4, `HOLD`, `MISSION_PAUSE`,
  `MISSION_START` and `EMERGENCY_HOVER` are all `MAV_CMD_DO_SET_MODE`, so an
  operator who clicks twice sends the same id twice.
- Some kinds need more than one MAVLink command: on ArduPilot a takeoff is a
  mode change and then the takeoff itself.
- An abort should still do something safe if the vehicle refuses the return.

The command logic also has to be testable without a vehicle, like
`CommandTransaction` and the setup sequence in M1.

## Decision

- **One client per vehicle, no I/O.** `CommandClient` takes time and acks as
  arguments and appends what to send and what to publish to an `Outbox`. The
  vehicle session owns one per vehicle and flushes the outbox after each call.
  Validation (`validate_request`) is a separate pure function that runs before
  a request reaches the client.
- **A request becomes a plan.** The autopilot adapter turns a request into a
  list of `CommandSpec` steps and an optional fallback. The steps go out one at
  a time; each must be `ACCEPTED` before the next is sent. A denied or failed
  step ends the request, except that if the last step is denied or fails and
  the plan has a fallback, the fallback goes out and decides the final state.
  The final `detail` records it (`MAV_CMD 20 denied (MAV_RESULT 2), sent
  fallback MAV_CMD 21`). `EMERGENCY_ABORT` uses this: return home, and land if
  the vehicle will not return.
- **The bus sees the request, not the steps.** A request publishes `RECEIVED`
  and `SENT` once, `IN_PROGRESS` for an `IN_PROGRESS` ack and again only when
  its progress changes, and one terminal status. Moving to the next step
  publishes nothing. `attempts` counts every send of every step, and
  `mav_result` is the last ack of the step in flight.
- **Deadlines are per step.** Each step is one `CommandTransaction` with the
  request's `deadline_ms` (or 1000 ms), counted from its own first send and
  retried at 350 ms and 700 ms. An `IN_PROGRESS` ack stops the retries and
  extends that step's deadline to 10 s after its first send, or to
  `deadline_ms` if that is longer, so it never shortens a deadline the caller
  asked for. A plan of two steps can therefore take up to twice the deadline.
- **A request claims its `MAV_CMD`s.** At submit, a request claims every
  `MAV_CMD` of its plan, steps and fallback. A request that needs a claimed
  one is `REJECTED_BUSY` at once, never queued, so an ack always belongs to
  exactly one request: the one whose current step has that command. A plan
  that uses the same `MAV_CMD` twice is refused for the same reason.
- **A command that may still be answered is quarantined.** A step keeps its
  `MAV_CMD` claimed for 700 ms after it ends in two cases:
  - It ended without a definitive ack of its own: it timed out, whether or
    not it had an `IN_PROGRESS` ack, or the bridge cancelled it at shutdown.
    The vehicle may still answer, and after `IN_PROGRESS` it owes a final
    ack.
  - It went out more than once and was answered. The vehicle acks every
    copy, and the copies go out over at most 700 ms, so over the same link
    the ack for the last copy arrives at most 700 ms after the ack that ended
    the step.

  Acks inside the quarantine are dropped instead of being credited to the
  next request for the same `MAV_CMD`. Only a step that went out once and got
  its definitive ack is released as soon as the request ends: its one answer
  has been used. A rejection during the quarantine says which case it is
  (`MAV_CMD 192 ended without an ack; waiting 700 ms for late acks`, or
  `MAV_CMD 176 was retried; waiting 700 ms for late acks`).
- **`CANCELLED` means two things only.** The vehicle answered
  `MAV_RESULT_CANCELLED`, or the bridge shut down while the command was in
  flight. A lost link does not cancel anything: the command times out at its
  deadline, which is what actually happened from the sender's point of view.
  `MAV_RESULT_CANCELLED` was a failure in M1; it gets its own state now
  because the contract has one.

## Consequences

Every request ends in exactly one terminal state, and an ack cannot be
credited to the wrong request while the same `MAV_CMD` is claimed or
quarantined. The cost is that a quick second command of a kind that shares a
`MAV_CMD` is refused rather than queued. On PX4, a second `HOLD` while the
first is still in flight, or a `MISSION_PAUSE` within 700 ms of a `HOLD` that
timed out or had to be resent, comes back `REJECTED_BUSY`, and the sender has
to try again. Queueing would hide that, and would need its own deadlines.

That applies to the emergency kinds as well. They can be `REJECTED_BUSY`
behind an ordinary command that shares their `MAV_CMD`. On PX4,
`EMERGENCY_HOVER` is `DO_SET_MODE` like `HOLD`, `MISSION_PAUSE` and
`MISSION_START`, and `EMERGENCY_ABORT` starts with the same return-to-launch
command as `RETURN_HOME`. While one of those is in flight or quarantined, the
emergency command is refused, not sent. Letting the emergency kinds pre-empt
an ordinary request (end it and take over its `MAV_CMD`) is a follow-up.

The quarantine is a bound, not a guarantee. An ack that arrives more than
700 ms after its command timed out can still be taken for the next request.
A longer quarantine would close more of that gap, at the price of keeping the
`MAV_CMD` busy for longer after every timeout.

Per-step deadlines make a multi-step kind slower to time out than its
`deadline_ms` suggests. The alternative, one deadline for the whole plan,
would leave later steps with whatever time the earlier ones did not use.
