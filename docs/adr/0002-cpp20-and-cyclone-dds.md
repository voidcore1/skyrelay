# 0002. C++20 core on Cyclone DDS

- Status: Accepted
- Date: 2026-11-23

## Context

SkyRelay bridges MAVLink vehicles to a DDS data space. The system design
(section 4) compares three ways to build it.

The first is a C++20 core on Cyclone DDS through cyclonedds-cxx, with
hand-written MAVLink state machines on top of the pinned MAVLink C headers,
Python for tooling and a TypeScript web UI. It is the hardest to build, but it
matches the stack used in real-time robotics, leaves a large latency margin
against the 100 ms p99 telemetry target, uses IDL natively, and forces me to
understand the protocol layer instead of delegating it.

The second is an all-Python implementation using pymavlink and
cyclonedds-python. It would take roughly half the effort, but the GIL
complicates per-vehicle isolation and the video path, and it says less about
the engineering the project is meant to show.

The third is ROS 2 on top of PX4's uXRCE-DDS client, or AP_DDS for ArduPilot.
It is popular, but it ties each path to a single autopilot, exposes
autopilot-internal message types and adds ROS topic and type mangling to the
contract. That is the opposite of the project's purpose, which is an
autopilot-neutral contract. ROS 2 interoperability stays on the roadmap as an
optional mode.

Constraints that apply regardless: the project is Apache-2.0, linked
dependencies must be permissive, and the code must build with GCC 15 on
Ubuntu 26.04 and GCC 13 in an Ubuntu 24.04 image, so only C++20 features are
allowed.

## Decision

I will implement the core in C++20 on Cyclone DDS 11.1 (cyclonedds-cxx 11.1),
with Boost.Asio and Boost.Beast for I/O, hand-written MAVLink state machines
on the pinned `c_library_v2` headers, Python for tooling and React with
TypeScript for the web UI. Fast DDS remains the fallback vendor and an interop
peer, not a build target.

## Consequences

- I carry the full cost of the MAVLink protocol layer (command lifecycle,
  link monitoring, signing) instead of leaning on MAVSDK. MAVSDK and
  pymavlink are used only as test peers.
- The data contract is IDL, shared by every language binding, with no
  vendor-specific types in it.
- Two compiler generations must stay green in CI, which rules out C++23.
- Cyclone DDS 11.1 is newer than what vcpkg's registry carries, so the
  dependency needs overlay ports (see ADR 0003).
