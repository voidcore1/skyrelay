# Changelog

All notable changes to SkyRelay are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Releases before
security lands (M3) carry a `-preview` suffix.

## [Unreleased]

## [0.1.0-preview] - 2026-11-25

> **INSECURE PREVIEW.** This release has no security at all. Run it only on a
> machine and network you trust, and do not expose it. The Security section of
> this release lists what is and is not in place.

The first walking skeleton: four simulated PX4 vehicles, through the bridge
and DDS, to a map in the browser, with the latency of the bridge measured.
Telemetry only.

### Added

- **Simulation:** `sim/sih-px4-4`, four headless PX4 v1.17.0 SIH quadrotors,
  one container and network namespace each, built from the PX4 release tag,
  each with its own home on a 45 m square; `sim/check_heartbeats.py` to check
  that every vehicle is sending.
- **MAVLink library:** `libskyrelay-mavlink` with UDP links, MAVLink 2 frame
  parsing and packing on vendored, pinned C headers, a link monitor
  (connected, degraded, lost, recovering), the setup sequence that requests
  message rates and `AUTOPILOT_VERSION`, PX4 and generic adapters, and a
  per-vehicle state model. The state machines take an injected clock and
  transport.
- **Bridge:** `skyrelay-bridge`, one session per vehicle from a YAML
  configuration, acting as a ground station (sysid 245, 1 Hz heartbeat). Each
  vehicle locks to one MAVLink system id, either the one configured with
  `sysid` or the first autopilot heartbeat on its link. It sets a vehicle up
  again after a reboot without dropping the link, and publishes under
  per-vehicle locks, dropping a failed write instead of throwing.
- **Data contract:** IDL v1 (`skyrelay::v1`) with five telemetry topics:
  `vehicle_info`, `vehicle_state`, `pose`, `health` and `link_status`. Every
  sample carries a common header (`seq`, `t_source_us`, `t_bridge_ns`,
  `valid_mask`); every struct is `@appendable`, keyed by `vehicle_id`, XCDR2.
  QoS profiles `telemetry_fast`, `telemetry_reliable` and `state_latched`, on
  DDS domain 42 with the fleet name as partition. Documented in
  `docs/contract.md` and `docs/contract/v1-fields.md`.
- **Code generation:** `tools/codegen/idl2gen.py` generates JSON converters,
  TypeScript types and the field table from the IDL, with a freshness check in
  CI.
- **Gateway:** `skyrelay-gateway` serves the station and the offline map, with
  HTTP range requests for PMTiles, and streams telemetry as JSON over a
  WebSocket (`pose` capped at 10 Hz per vehicle per client, latest-value
  fan-out, late joiners get the latched topics). Documented in
  `docs/gateway-protocol.md`.
- **Web station:** offline MapLibre map from a PMTiles extract fetched once by
  `tools/maps/fetch-region.sh` with pinned, checksum-verified downloads; fleet
  panel with link states, mode, armed state and battery, and a stale-data
  notice when the gateway connection drops; pose-age HUD (nearest-rank p50 and
  p99 over 10 s) and the round trip to the gateway, labelled indicative.
- **Standalone demo:** the station at `?demo=1` with synthetic vehicles on a
  schematic map and a scripted 12 s link replay, labelled synthetic
  throughout.
- **Benchmark:** `skyrelay-bench-probe`, `bench/run.sh` and
  `bench/report/latency.py`. One command brings up the simulation and the
  bridge, records `pose` for 60 s, reports nearest-rank p50/p95/p99 per
  vehicle and overall, and gates on p99 ≤ 100 ms.
- **First published result:** M1 clean profile, overall p99 0.260 ms from
  bridge decode to DDS consumer (`docs/benchmarks/2026-11-23-m1-clean.md`).
  This excludes the vehicle and the link.
- **Build:** CMake presets (`dev`, `asan`, `tsan`, `release`) and a vcpkg
  manifest with a pinned baseline, with overlay ports for Cyclone DDS 11.1.0
  and cyclonedds-cxx 11.1.0.
- **CI:** C++ build and unit tests on Ubuntu 24.04 (GCC 13) and 26.04
  (GCC 15), codegen and Python tests, web station tests, lint and build.
- **Licence check:** `tools/check_licenses.py`; CI fails if any port installed
  through vcpkg is under the GPL or AGPL.
- **Nightly benchmark:** `sitl-nightly` runs the m1-clean benchmark end to end
  on a CI runner every night and keeps the results as an artifact.
- **Docs:** system design, ADRs 0001–0006, simulation and benchmark guides, and
  the landscape research behind the project.

### Security

- **No security yet.** There is no DDS-Security, no MAVLink signing, and no TLS
  or authentication at the gateway. These arrive in M3.
- DDS is restricted to loopback unless `--insecure-allow-network` is given.
- The gateway listens on loopback only, checks `Host` on every request and
  `Origin` on the WebSocket upgrade, and refuses non-loopback listen
  addresses.
- The station shows an INSECURE PREVIEW banner, which cannot be closed,
  whenever the gateway reports itself insecure.
- The bridge's MAVLink UDP ports (14540–14543 in the simulation configuration)
  listen on every interface and accept unsigned traffic.

[Unreleased]: https://github.com/voidcore1/skyrelay/compare/v0.1.0-preview...HEAD
[0.1.0-preview]: https://github.com/voidcore1/skyrelay/releases/tag/v0.1.0-preview
