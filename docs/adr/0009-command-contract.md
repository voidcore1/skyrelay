# 0009. Command contract: partitions as authority, random ids, one status per state

- Status: Accepted
- Date: 2026-11-25

## Context

M2 lets other programs send commands and missions to a vehicle through the
bridge. The data contract needs a request topic, a status topic and a mission
topic, and rules for who may send what, how a command is named, and how a
sender follows what happened to it. The preview has no authentication yet, so
whatever protects the dangerous commands has to work with plain DDS.

## Decision

- **Partitions are the command authority.** `command_request` is written on
  `<fleet>.cmd`, and `EMERGENCY_KILL` only on `<fleet>.cmd.kill`. The bridge
  reads both, and rejects a kill that arrives on the wrong one as
  `REJECTED_UNAUTHORISED`. A participant that was never given the partition
  cannot send the command, with no extra protocol. A kill also carries
  `confirm_token == command_id XOR vehicle_id`. Both are guards against sending
  by accident, not a security control; DDS-Security replaces them in M3.
- **Command ids are random, not counters.** `command_id` is a random `uint64`
  from `getrandom(2)`, never 0, and is part of the key together with
  `vehicle_id` and `issuer_id`. A counter would collide after a restart, or
  between two issuers that start at the same time. `issuer_id` is a static
  `uint32` per issuer, and is attribution only. Ids travel as decimal strings
  in JSON (ADR-0008).
- **One status sample per state.** `command_status` has the same key as the
  request, KEEP_LAST 1 per instance and TRANSIENT_LOCAL, so a late reader gets
  the latest state of each command. The bridge writes a sample for every state
  a command enters (`RECEIVED`, `SENT`, `IN_PROGRESS`, then one terminal
  state), and the transition times are fields: `t_received_ns`, `t_sent_ns`,
  `t_in_progress_ns` and `t_terminal_ns`, each with a valid bit. The ack
  latency can then be read from one sample, and a recording holds the whole
  history.
- **`command_request` is the one topic with several producers.** `header.seq`
  counts per issuer and vehicle, `header.t_bridge_ns` is the issuer's
  `CLOCK_REALTIME` at write time, and `header.t_source_us` is 0. The key
  includes `issuer_id`, so writers never share an instance.
- **NaN means unused.** `CommandParams` has one field per parameter any kind
  uses, and a kind ignores the rest. A field the kind reads but may leave
  alone, such as the speed of a `GOTO`, is NaN for "keep the current value".
  0 is always a value. A struct per kind would need a union, which the IDL
  rules forbid (ADR-0006).
- **`operator_name`, not `operator`.** `operator` is a C++ keyword, and the
  generated accessor would not compile.
- **Field tables are generated.** The per-field tables for the new types live
  in the generated `docs/contract/v1-fields.md`, and `docs/contract.md` links to
  them instead of repeating them. The prose in `docs/contract.md` covers what
  the tables cannot: the lifecycle, the key rules, the per-kind parameters and
  the validation ranges.
- **`mission_state` has no core fields.** What autopilots report about a
  mission varies, so all its fields are extended in v1.

## Consequences

Anyone who can join the partition can send a command, so the partition names
are part of the deployment, not a secret. Random ids mean a reader cannot order
commands by id and has to use `header.seq` and the timestamps. Clients see one
sample per state rather than one growing record, which costs a few more
samples and keeps every sample immutable. The contract version becomes
`1.0.0-preview.2`.
