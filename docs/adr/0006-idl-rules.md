# 0006. IDL rules, XCDR2 and a loopback-only DDS domain

- Status: Accepted
- Date: 2026-11-23

## Context

The IDL in `idl/skyrelay/v1/types.idl` is the public contract: anything that
speaks DDS can read SkyRelay telemetry from it, and I will generate the JSON
and TypeScript views from the same file. Whatever I put there I have to keep
compatible for as long as v1 runs.

Cyclone DDS 11.1 defaults every type to `final`, which cannot be extended
without breaking readers. Its IDL support for unions has had renumbering
problems and it has no maps or bitsets, and other DDS vendors handle those
constructs differently.

The M1 preview also has no authentication, so it must not be reachable from the
network by accident.

## Decision

I will keep the IDL small and append-only.

- Everything lives in module `skyrelay::v1`, topics are named
  `skyrelay/v1/<name>`, and a breaking change means a `v2` module and new
  topics. Cyclone accepts the `/` in topic names, so the names are used as
  written.
- Every struct is explicitly `@appendable`, every per-vehicle type has
  `@key uint32 vehicle_id`, and the wire format is XCDR2 (the encoding that
  supports `@appendable`).
- Unions, maps, bitsets, `@optional` and recursive types are not used.
  Missing data is expressed by the header's `valid_mask` instead, so a reader
  never needs optional members.
- idlc 11.1 accepts `@appendable` on enums, so enums carry it too. Enum values
  are still append-only by rule: a new value goes last, an existing one is never
  reordered, removed or reinterpreted. The first literal is always the
  unknown or zero state, and literal names are prefixed with the enum's own
  prefix (`AUTOPILOT_`, `FLIGHT_MODE_`, `LINK_` and so on) because IDL enum
  literals share the module's namespace.
- Every data field has a trailing `// unit: <unit>` comment on the same line, so
  the contract table can be generated from the IDL.
- The generated C++ is built as a separate target (`skyrelay_idl`) with its
  include directories marked SYSTEM, so `-Werror -Wconversion` only ever applies
  to my own sources. The generated header is called `types.hpp` and ends up
  flat in the build tree, so code includes `skyrelay/dds/idl.hpp` instead.

The default domain is loopback only, which I implement in
`cyclone_config_xml()` (`lib/dds/src/domain.cpp`):

- `<NetworkInterface address="127.0.0.1" multicast="false"/>` restricts
  Cyclone to the loopback interface;
- `<AllowMulticast>false</AllowMulticast>`, since loopback has no usable
  multicast;
- discovery uses the unicast peer `127.0.0.1` with
  `<ParticipantIndex>auto</ParticipantIndex>`, which gives every participant its
  own port, so several participants in one process or in several processes find
  each other.

The configuration is passed to `dds_create_domain()` before the participant is
created, so it does not depend on the `CYCLONEDDS_URI` environment variable. If
the domain already exists in the process, the existing one is reused. Setting
`allow_network` drops the interface and peer restrictions; the bridge exposes
it only as `--insecure-allow-network`.

## Consequences

- Readers written against v1 keep working when I append fields, as long as I
  respect the append-only rule. Nothing in the toolchain enforces that for
  enum values, so I have to catch a reordering in review.
- Without unions or `@optional`, every field is always on the wire, even when
  the vehicle cannot supply it. That costs a few bytes per sample and is
  the price of a simple contract.
- Two processes on one host can talk, a different host cannot until
  `allow_network` is set. A second participant in the same process shares the
  configuration of the first, so the first call to `open_participant()` wins.
- The unit comments have to stay on the field's line or the generated contract
  table loses them.
