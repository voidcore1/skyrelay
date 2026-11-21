# SkyRelay

**A vendor-neutral, secure-by-default bridge between MAVLink drones and DDS — built for multi-vehicle operations and measured, not just claimed.**

> Status: design phase. No runnable code yet.

## Why

Drones speak **MAVLink**. Larger systems — control stations, mission software, analytics — increasingly speak **DDS**, the publish/subscribe standard used across robotics and real-time systems.

Today every maintained path from a drone onto DDS belongs to a single autopilot (PX4's uXRCE-DDS, ArduPilot's AP_DDS) and exposes that autopilot's internal messages. Autopilot-agnostic tools either require ROS, handle one vehicle at a time, or forward raw frames without a typed data model.

SkyRelay fills that gap: one bridge, one versioned data model, PX4 and ArduPilot alike, several vehicles at once.

## What it is

The core product is a **contract**:

- a **versioned IDL data model** for telemetry, commands and payload state,
- **QoS and security profiles** (DDS-Security + MAVLink 2 signing, on by default),
- a **reproducible benchmark method** with published latency percentiles.

The bridge, web control station and KLV video encoder are reference implementations of that contract.

```
PX4 / ArduPilot ──MAVLink──► SkyRelay bridge ──DDS──► any DDS consumer
   (2–4 vehicles)                 │                    (web station, apps, ROS 2)
                                  ├── record / replay (MCAP)
                                  ├── video + KLV (MISB ST 0601) via RTSP/WebRTC
                                  └── fault-injection benchmark harness
```

## Targets

| Metric | Target |
|---|---|
| Telemetry latency (end-to-end) | ≤ 100 ms |
| Command acknowledgement | ≤ 1 s |
| Video latency | ≤ 300 ms |
| Lost-link detection & recovery | ≤ 5 s |
| Data reliability | ≥ 99 % |

Every number will be reported as p50 / p95 / p99 under named network-impairment profiles, reproducible with one command.

## Planned stack

C++20 · Cyclone DDS 11 · MAVLink C headers (own protocol state machines) · PX4 v1.17 and ArduPilot 4.7 SITL on Gazebo Harmonic (Docker) · GStreamer + MediaMTX · MapLibre GL JS with offline PMTiles · MCAP · CMake.

## Roadmap

1. MAVLink core + IDL, verified against multiple PX4 and ArduPilot SITL vehicles
2. MAVLink signing, DDS-Security, cross-vendor secure-interop matrix
3. Network fault-injection and benchmark harness
4. Browser gateway and web control station
5. Video with synchronized KLV metadata
6. Fully offline, signed release with SBOM

## Docs

- [Landscape research (Nov 2026)](docs/research/landscape-2026-11.md) — prior art, differentiators, tool versions and pitfalls

## License

[Apache-2.0](LICENSE)
