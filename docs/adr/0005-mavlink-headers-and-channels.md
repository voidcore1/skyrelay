# 0005. MAVLink C headers, one channel per session

- Status: Accepted
- Date: 2026-11-23

## Context

The bridge has to speak MAVLink 2 to ArduPilot and PX4 vehicles, so it needs
message definitions for the `ardupilotmega` dialect and a frame parser. The
options are MAVSDK, libmav, or the C headers that the MAVLink project
generates (`c_library_v2`).

The C headers keep their parser and sequence state in static per-channel
arrays. `MAVLINK_COMM_NUM_BUFFERS` sets the array size (16 on Linux by
default), and an index past it overruns the stack. The helper functions are
`static inline`, so every translation unit that includes the headers gets its
own copy of those arrays.

## Decision

I will use the generated C headers directly, vendored under
`third_party/mavlink`, and not MAVSDK or libmav.

- MAVSDK is a full client library with its own threading, connection and
  plugin model. SkyRelay needs a parser and a packer under its own session
  and timing control, and I do not want a second runtime to reconcile with
  the DDS side. libmav is a smaller header-only option, but it is a younger
  project, and the generated headers are the reference implementation that
  every other tool is checked against.
- `third_party/mavlink/PIN` records the repository, commit, date and dialect.
  To update, I check out a new commit of `c_library_v2`, follow the steps in
  `third_party/mavlink/README.md`, and update `PIN` in the same commit. Only
  `ardupilotmega` and the dialects it includes are kept.
- `skyrelay/mav/mavlink.hpp` is the single include point. It defines
  `MAVLINK_COMM_NUM_BUFFERS` as 16 before including the headers, so no other
  file includes them directly. The vendored directory is a SYSTEM include so
  `-Werror -Wconversion` applies only to SkyRelay sources.
- Each vehicle session owns exactly one channel (0-15) for the lifetime of the
  process. Its `Framer` and `Packer` use that channel, and nothing else does.
  Different channels share no state, so sessions on different threads need no
  locking between them.
- The bridge therefore supports at most 16 simultaneous vehicle sessions per
  process. Opening a 17th is rejected rather than wrapped onto a used channel.
- Because the channel state is per translation unit, receive state is touched
  only from `framer.cpp` and send state (the sequence counter) only from
  `packer.cpp`. Other code calls the MAVLink pack and parse functions
  through those classes.

## Consequences

- The dependency is small, has no runtime of its own, and the generated code
  matches the pinned dialect exactly.
- The 16-session cap is a hard limit. Raising it means changing the macro and
  accepting the larger static arrays, which is a one-line change but affects
  every translation unit, so it has to be defined in the one header.
- The per-translation-unit state is easy to misuse: calling a `_pack_chan`
  function from a new source file would use a separate sequence counter. The
  rule above has to be kept in review.
- I own the vendored copy. Updating it is a manual step, and a dialect that
  starts including another dialect needs that directory copied as well.
