# SkyRelay — System Design

- **Status:** Accepted (M0–M1 implemented)
- **Date:** 2026-11-23
- **Scope:** whole project, v1.0
- **Background research:** [docs/research/landscape-2026-11.md](../research/landscape-2026-11.md)

---

## 1. Intent

SkyRelay is a vendor-neutral data plane between MAVLink drones and DDS. It lets any DDS application see and command several drones, PX4 or ArduPilot, through one versioned data model. It is secure by default, and every performance claim is backed by a reproducible benchmark.

**Why it should exist.** Every maintained route from a drone onto DDS belongs to one autopilot (PX4 uXRCE-DDS, ArduPilot AP_DDS) and exposes that autopilot's internal messages. The tools that work with any autopilot either need ROS (MAVROS), serve one vehicle (MAVSDK server), or forward untyped frames (routers). Nobody publishes end-to-end multi-vehicle latency under impaired links, or cross-vendor DDS-Security interop results. See the research doc for evidence.

**Who it is for.** Integration engineers judging whether the project is credible. They must be convinced twice:

1. **In two minutes.** A live demo: several drones on a map, commands that round-trip visibly, latency numbers on screen.
2. **Under scrutiny.** One command reproduces every number. Tests, replays, a threat model, and decision records explain *why* each choice was made.

**The product is a contract.** That means the IDL data model, the QoS and security profiles, the command lifecycle, and the benchmark method. The bridge, gateway, web station and KLV codec are reference implementations of that contract.

**It is also a learning vehicle.** Every significant decision is recorded as an ADR (`docs/adr/NNNN-title.md`), and each milestone lists the concepts it exercises.

## 2. Goals, non-goals, anti-goals

### Goals

| ID | Goal | Done when |
|---|---|---|
| G1 | Autopilot-agnostic bridge | The contract conformance checker (§6) passes on the same scripted scenario for PX4 v1.17, PX4 v1.18 and ArduPilot Copter 4.7 SITL: same topics, same types, every *core* field valid, all values within documented semantics |
| G2 | Versioned, documented data contract | IDL v1 published; an XTypes evolution test passes between v1.0 and v1.1 types |
| G3 | Multi-vehicle with isolation | 4 vehicles concurrently. Impairing one vehicle's link keeps the others' telemetry p99 within the isolation tolerance in §10 |
| G4 | Explicit command lifecycle | Every command request reaches a terminal state, and timeout rate ≤ 1 % on gated profiles (§10) |
| G5 | Measured performance | p50/p95/p99 for all metrics in §10 under every named network profile, from one command. Pass/fail gates apply on the gated profiles |
| G6 | Secure by default | DDS-Security and MAVLink 2 signing are on unless `--insecure` is given, which logs a loud warning. The secure-interop matrix is published |
| G7 | Record and deterministic replay | Replaying a recording through the bridge reproduces identical DDS output (bridge timestamps excepted) |
| G8 | Video with synchronized KLV | Per-frame MISB ST 0601 metadata reaches the browser. The camera footprint is drawn on the map. Video latency is measured |
| G9 | Web control station | Fleet view, per-vehicle command panel, video tiles and a live latency HUD. Works with no internet |
| G10 | Offline, verifiable release | A clean build and deployment on a network-isolated host from one bundle, with an SBOM and signatures |

### Non-goals (v1)

- Feature parity with full ground stations. No parameter-editing UI, log analysis, survey-pattern mission planner, or firmware flashing.
- Flight control, autonomy or swarm algorithms. SkyRelay moves data and commands; it does not decide where drones fly.
- Requiring real flight hardware. Everything works in simulation, and HITL is a stretch goal.
- A ROS dependency in the core. ROS 2 compatibility is a stretch goal, as an optional mode.
- Windows or macOS hosts.
- Link-layer radio management, cellular or satcom.
- More than 16 vehicles. The design must not preclude it, but nothing is tuned for it.

### Anti-goals (failure even if it "works")

- Numbers that can't be reproduced by someone else from the repo.
- A demo that only runs on my machine.
- Autopilot-specific types or concepts leaking into the IDL.
- Security bolted on at the end, or off by default.
- Growing into "yet another ground station".
- Any organisation-specific or sector-specific branding in the repo.

## 3. Constraints

- **Host:** Ubuntu 26.04 LTS, GCC 15, Python 3.14, 20 cores, 15 GB RAM, RTX 3050 4 GB.
  - PX4 and Gazebo do not support 26.04, so all simulation runs in **Ubuntu 24.04 containers** (Docker Engine + NVIDIA Container Toolkit ≥ 1.20.2).
  - SkyRelay binaries must build both natively on 26.04 (GCC 15) and in a 24.04 builder image (GCC 13). C++20 only; no C++23 features.
- **Licences:** the project is Apache-2.0. Linked dependencies must be permissive:
  - Cyclone DDS: EPL-2.0 / EDL-1.0
  - MAVLink generated C headers: MIT
  - Boost: BSL
  - MCAP: MIT
  - GoogleTest: BSD
  - MapLibre: BSD
  - MediaMTX runs as a separate process (MIT).
  - No GPL code is linked into SkyRelay binaries or shipped in SkyRelay's **runtime** images (bridge, gateway, video, recorder, MediaMTX).
  - The **simulation** environment is a separate development and test tool built from upstream, publicly available sources (PX4, ArduPilot, Gazebo, and their camera plugins, which may encode with x264). It is not part of the SkyRelay product. When it is redistributed (§15), it ships as a separate bundle with licence notices and corresponding GPL sources.
- **Offline:** every build input is pinned and can be vendored or mirrored. Builds are proven with `--network=none`.
- **Version churn:** PX4 v1.18 final will land mid-project. It changes signing provisioning and DDS namespaces. Both v1.17.0 and v1.18.x are supported targets.
- **No organisation or sector references** anywhere in the repo.

## 4. Approaches considered

| | Approach | Trade-off | Verdict |
|---|---|---|---|
| A | **C++20 core on Cyclone DDS (cyclonedds-cxx 11.1)**, hand-written MAVLink state machines on pinned C headers, Python for tooling, TypeScript web UI | Hardest to build. Matches the stack used in real-time robotics, has large latency headroom and native IDL. Writing the protocol layer by hand forces deep understanding | **Chosen** |
| B | All Python (pymavlink + cyclonedds-python) | Roughly 2× faster to build. The GIL complicates the per-vehicle isolation and video paths | Rejected |
| C | ROS 2 on PX4's uXRCE-DDS (+ AP_DDS) | ROS 2 is widely used and has a large ecosystem, but it locks each path to one autopilot, exposes autopilot-internal types, and adds ROS topic and type mangling to the contract. That is the opposite of the project's reason to exist | Rejected; ROS 2 interop becomes a stretch mode instead |

**Sub-decisions within A:**

- **MAVLink library.** Pinned `c_library_v2` (MIT, `ardupilotmega` dialect, which includes `common`), plus our own state machines.
  - MAVSDK hides exactly the layer this project is about.
  - libmav is pre-1.0 (v0.0.2).
  - MAVSDK 4.2.x and pymavlink 2.4.50 are used as **test peers** only.
- **DDS vendor.** Cyclone DDS 11.1.x. Fast DDS 3.6.2 LTS is the fallback and an interop peer.
- **Fixed by this design:**
  - **I/O library:** Boost.Asio and Boost.Beast throughout, one async model.
  - **Web UI:** React + TypeScript + Vite.
  - **Map:** MapLibre GL JS 6.x with PMTiles.

## 5. Architecture

```
            ┌──────────────── simulation (Ubuntu 24.04 containers) ───────────────┐
            │  gz-server ── px4/ardupilot vehicle 0..N-1 (one container each)     │
            │      │ MAVLink UDP            │ RTP/H.264 UDP 5600+N                 │
            │  [link-emulator sidecar per vehicle: tc netem]                      │
            └──────┼────────────────────────┼─────────────────────────────────────┘
                   ▼                        ▼
         ┌──────────────────┐      ┌──────────────────┐
         │ skyrelay-bridge  │      │ skyrelay-video   │  (one per camera vehicle)
         │  per-vehicle     │      │  GStreamer +     │──RTSP (H.264 + RFC 6597 KLV)──► MediaMTX
         │  sessions        │      │  libskyrelay-klv │                                   │ WHEP + KLV datachannel
         └───────┬──────────┘      └────────┬─────────┘                                   ▼
                 │        DDS (Cyclone, DDS-Security)                                 browser
   ══════════════╪══════════════════════════╪══════════════════════════════════         ▲
                 │                          │                                           │ WSS (JSON)
         ┌───────┴──────────┐     ┌─────────┴──────────┐      ┌────────────────────┐    │
         │ skyrelay-record  │     │ skyrelay-gateway   │──────┤ web station (SPA)  │────┘
         │ (MCAP, in-bridge │     │ DDS ⇄ WebSocket,   │      │ map, fleet, cmds,  │
         │  option)         │     │ auth, static files │      │ video, latency HUD │
         └──────────────────┘     └────────────────────┘      └────────────────────┘
                 ▲
         skyrelay-bench (probe DDS participant + scenario runner + report)
```

### Processes and libraries

| Component | Language | Responsibility |
|---|---|---|
| `libskyrelay-mavlink` | C++20 | Links (UDP/TCP/serial), framing, signing, microservice state machines, autopilot adapters. **No DDS dependency, no I/O in the state machines** (clock and transport injected) |
| `libskyrelay-dds` | C++20 | Generated IDL types, QoS profiles, mapping between MAVLink-derived domain structs and IDL samples |
| `skyrelay-bridge` | C++20 | Process wiring: config, per-vehicle sessions, DDS participant, optional MCAP recording |
| `skyrelay-gateway` | C++20 | DDS participant ⇄ WebSocket (TLS), operator auth and command authorisation, rate limiting toward browsers, serves the static SPA and offline map tiles |
| `skyrelay-video` | C++20 + GStreamer 1.28 | Per camera vehicle: ingest RTP H.264, subscribe to DDS pose and payload, build ST 0601 KLV per frame, compute footprint, publish RTSP to MediaMTX |
| `libskyrelay-klv` | C++20 | Standalone ST 0601.19 encoder/decoder with no other SkyRelay dependency |
| `skyrelay-replay` | C++20 | Feeds an MCAP recording back through the bridge core with an injected clock |
| `skyrelay-keys` | C++20 or Python | Offline generation of MAVLink signing keys and the DDS-Security CA and identities |
| `bench/` | Python 3.12+ (runs in the container) + C++ probe | Scenario runner, network profiles, latency probe, report generator |
| `web/station` | TypeScript, React, Vite | Operator UI |

**Why the gateway is a separate process.** The bridge stays minimal and safety-relevant. The gateway is just another DDS participant, which proves the contract can be consumed by third parties. The browser sits outside DDS-Security, so the gateway is the authorisation boundary.

### Bridge internals

```
VehicleSession (one per configured vehicle, own thread + io_context)
  Link (udp|tcp|serial) → Framer/Parser → SigningVerifier
     → Router by msgid → Microservices:
          LinkMonitor · CommandClient · MissionClient · ParamClient
          · MessageIntervalManager · GimbalClient · CameraClient
     → AutopilotAdapter (Px4Adapter | ArduPilotAdapter) normalises modes, commands and quirks
     → DomainState (latest normalised vehicle state)
     → Publisher (rate-capped per topic) → DDS writers
  CommandInbox (bounded MPSC queue) ← DDS CommandRequest reader (filtered by vehicle_id)
```

- **Isolation.**
  - Each vehicle session runs on its own thread with its own `io_context`. A stalled or misbehaving vehicle can't block another.
  - The same binary can also run **one process per vehicle** (`--vehicle <id>`), for OS-level fault isolation. The compose files show both modes.
- **Backpressure.**
  - Telemetry is latest-value: if the publisher is behind, older samples are overwritten.
  - Commands are never dropped silently. If a vehicle's inbox is full, the command is immediately terminal `REJECTED_BUSY`.
- **The bridge is the GCS** from each vehicle's point of view.
  - It sends a 1 Hz `HEARTBEAT` with `MAV_TYPE_GCS`.
  - Its system ID is configurable (default **245**, to avoid colliding with QGroundControl's 255). Component ID is `MAV_COMP_ID_MISSIONPLANNER`.
  - Vehicle failsafes (PX4 `COM_DL_LOSS_T`, ArduPilot `FS_GCS_*`) are the autopilot's business. The docs explain how bridge heartbeats interact with them.

## 6. Data contract (IDL v1)

### Rules

- **Module and topics.** Module `skyrelay::v1`. Topic names are `skyrelay/v1/<name>`. A breaking change means a new module and new topics (`v2`); v1 keeps running.
- **Types.**
  - Every struct is annotated `@appendable`. Cyclone's default is `final`, so this must be explicit everywhere.
  - Every per-vehicle type has `@key uint32 vehicle_id`.
  - The XCDR version is pinned (XCDR2) and documented.
- **Forbidden in IDL:** unions, maps, bitsets, `@optional` and recursive types.
  - Reasons: cross-vendor fragility, Cyclone 11.1's union ID renumbering, and Cyclone's lack of maps and bitsets.
  - Variant data uses an enum discriminator plus a parameter struct whose fields are documented per variant.
- **Evolution.** Fields may only be appended. Enum values may only be appended. Removing or reinterpreting anything requires `v2`.
- **One producer role per topic.** Exactly one process role writes each topic (see the Producer column), so `seq` stays monotonic and samples from different writers never overwrite each other.
- **Common header, in every sample:**
  - `uint64 seq`: per vehicle per topic, monotonic.
  - `uint64 t_source_us`: the vehicle's timestamp. `time_boot` is converted to UTC via `SYSTEM_TIME` when available; 0 if unknown.
  - `uint64 t_bridge_ns`: `CLOCK_REALTIME` when the bridge decoded the source MAVLink message.
  - `uint64 valid_mask`: one bit per field (or documented field group) in that struct. A cleared bit means the source cannot supply the value. Floating-point fields whose bit is cleared are also set to NaN, so careless readers fail loudly. Integer and enum fields whose bit is cleared are 0.
- **Core fields.** For each topic, `docs/contract.md` marks the *core* fields: those every supported autopilot must supply. Everything else is *extended*: valid when available.
- **Conformance checker.** `tools/conformance/` reads an MCAP recording or live DDS and checks every sample:
  - the core fields' valid bits are set;
  - values are in range with correct units (latitude in degrees, quaternion normalised, and so on);
  - `seq` is monotonic;
  - QoS matches the contract.

  G1 runs it against the same scripted scenario on every supported autopilot.
- **Units:** SI units, WGS-84 degrees as `double`, altitude in metres with the reference stated in the field name (`alt_msl_m`, `alt_rel_home_m`), NED frame for velocities, quaternions for attitude (w, x, y, z). Every field is documented in the IDL comments and mirrored in `docs/contract.md`.

### Topics

| Topic | Producer | Content (abridged) | QoS profile |
|---|---|---|---|
| `vehicle_info` | bridge | autopilot kind (`PX4`, `ARDUPILOT`, `OTHER`), firmware version, vehicle type, MAVLink sys/comp ID, capability flags (`uint64`), contract version string | `state_latched` |
| `vehicle_state` | bridge | armed, normalised flight mode enum + raw mode string, landed state, system status | `telemetry_reliable` |
| `pose` | bridge | lat/lon/alt_msl/alt_rel_home, attitude quaternion, NED velocity, heading | `telemetry_fast` |
| `health` | bridge | battery (V, A, %, time remaining), GNSS (fix type, sats, hdop), sensor health flags, EKF/estimator ok | `telemetry_reliable` |
| `link_status` | bridge | state (`CONNECTED`, `DEGRADED`, `LOST`, `RECOVERING`), since, rx rate, loss % (from MAVLink seq gaps), last rx age, signing state | `state_latched` |
| `mission_state` | bridge | mission ID/hash, item count, current item, progress, last upload result | `state_latched` |
| `payload_state` | bridge | gimbal attitude (relative and absolute), camera mode, zoom, HFOV/VFOV, recording flag | `telemetry_reliable` |
| `footprint` | video | ground footprint (4 corner lat/lon), frame centre, slant range, method (`FLAT_EARTH` or `DEM`), frame timestamp | `telemetry_reliable` |
| `video_stream_info` | video | RTSP and WHEP URLs, codec, resolution, KLV present | `state_latched` |
| `command_request` | gateway, authorised apps | key: `vehicle_id`, `issuer_id`, `command_id`. `command_id` is a random `uint64` from a CSPRNG, so it is unique across issuer restarts without counters. `issuer_id` is `uint32`, statically assigned per identity (§8). `operator` is a `string<64>` for audit attribution. Also: `CommandKind` enum, `CommandParams` struct, `sequence<Waypoint>` for missions, deadline_ms, confirm_token | `command` |
| `command_status` | bridge | same key as the request, state (see §7), raw `MAV_RESULT`, progress %, detail string, timestamps per transition | `command_status` |

**v1 command kinds:**
- `ARM`, `DISARM`, `TAKEOFF(alt)`, `LAND`, `RETURN_HOME`, `HOLD`
- `GOTO(lat, lon, alt, ground_speed?)`, `SET_SPEED`, `SET_ALTITUDE`, `SET_HEADING`
- `MISSION_UPLOAD(items)`, `MISSION_START`, `MISSION_PAUSE`, `MISSION_CLEAR`
- `GIMBAL_POINT(pitch, yaw)`, `CAMERA_ZOOM(level)`, `CAMERA_CAPTURE`, `VIDEO_START`, `VIDEO_STOP`
- `EMERGENCY_ABORT` (return home, or land if home is invalid), `EMERGENCY_HOVER`
- `EMERGENCY_KILL` (flight termination). This must arrive on the kill command partition (§8). It also needs `confirm_token == command_id XOR vehicle_id`, a guard against accidental sends, not a security control.

### QoS profiles

Defined once in `libskyrelay-dds`, documented in `docs/contract.md`. Also shipped as DDS-XML if Cyclone 11.1 supports XML QoS profiles; confirmed in M0.

| Profile | Reliability | History | Durability | Other |
|---|---|---|---|---|
| `telemetry_fast` | RELIABLE | KEEP_LAST 1 | VOLATILE | deadline 200 ms, liveliness AUTOMATIC lease 1 s |
| `telemetry_reliable` | RELIABLE | KEEP_LAST 1 | VOLATILE | deadline 1 s |
| `state_latched` | RELIABLE | KEEP_LAST 1 | TRANSIENT_LOCAL | — |
| `command` | RELIABLE | KEEP_ALL with resource limits (64 per instance) | VOLATILE | lifespan = 5 s |
| `command_status` | RELIABLE | KEEP_LAST 1 per instance (key: vehicle_id + issuer_id + command_id) | TRANSIENT_LOCAL | lifespan 60 s |

- **Partitions and domain.** DDS domain ID comes from config (default 42). The data partition is the fleet name (config, default `default`). Commands use two partitions per fleet, which give command authority (§8):
  - `<fleet>.cmd` for normal commands
  - `<fleet>.cmd.kill` for `EMERGENCY_KILL`
- **Per-vehicle subscription.** Consumers use content-filtered topics on `vehicle_id`. Topics are not split per vehicle.

### Default publish rates

Configurable; the bridge decimates if the source is faster.

| Topic | Rate |
|---|---|
| pose | 20 Hz |
| vehicle_state | 5 Hz + on change |
| health | 2 Hz + on change |
| payload_state | 10 Hz |
| link_status | on change + 1 Hz |

## 7. Protocol behaviour

### Command lifecycle

```
RECEIVED → (validation fails) → REJECTED_INVALID | REJECTED_UNSUPPORTED | REJECTED_BUSY | REJECTED_UNAUTHORISED
RECEIVED → SENT → [IN_PROGRESS(progress)]* → ACCEPTED | DENIED | FAILED | TIMED_OUT | CANCELLED
```

- **Terminal states:** every `REJECTED_*`, `ACCEPTED`, `DENIED`, `FAILED`, `TIMED_OUT`, `CANCELLED`.
- **Who rejects what:**
  - The bridge publishes every state on DDS, including `REJECTED_UNAUTHORISED`. It uses that state for an `EMERGENCY_KILL` that arrived on the normal command partition, and for an `issuer_id` not registered for the partition a command arrived on.
  - The gateway rejects unauthorised browser requests *before* they reach DDS. It reports them only to that browser over the WebSocket, and records them in the audit log. Gateway rejections never appear on the bus, because no DDS command was ever issued.
- **What `ACCEPTED` means.** The autopilot accepted the command (`MAV_RESULT_ACCEPTED`). It does not mean the manoeuvre completed. Completion is observable in telemetry; v1 does not track it as command state.
- **Retries.** Immediate commands resend `COMMAND_LONG`/`COMMAND_INT` at 350 ms and 700 ms, with an incremented `confirmation` field. If nothing arrives by `deadline_ms` (default 1000 ms), the state is `TIMED_OUT`.
- **`IN_PROGRESS`** from the autopilot extends the deadline to the command-kind maximum (configurable; default 10 s).
- **One in-flight command per (vehicle, MAV_CMD).** A second request for the same MAV_CMD while one is in flight is immediately `REJECTED_BUSY`. Different MAV_CMDs may overlap, as the MAVLink command protocol permits.
- **Missions** use the MAVLink mission protocol:
  - 1500 ms default timeout, 250 ms per item, 5 retries.
  - Progress is reported as `IN_PROGRESS` with a percentage.
  - **ArduPilot:** item 0 is home, uploads are not atomic, and there is no mission cancel. The adapter handles these. The `command_status` detail field says when a partial upload may remain on the vehicle after failure.

### Link monitoring and lost-link

MAVLink's convention (link lost after 4–5 missed 1 Hz heartbeats) would consume most of the 5 s budget. The bridge instead watches *all* received traffic, which arrives at ≥ 20 Hz once message intervals are set.

| Transition | Rule (defaults, configurable) |
|---|---|
| CONNECTED → DEGRADED | no MAVLink message for 250 ms, or seq-gap loss > 20 % over 1 s |
| DEGRADED → LOST | no MAVLink message for 1000 ms |
| LOST → RECOVERING | first valid (and, if signing is on, correctly signed) message |
| RECOVERING → CONNECTED | re-request message intervals, re-read mode and mission state, then 500 ms of healthy traffic |

**Targets:**
- Detection (blackout start → `LOST` published): **≤ 1.5 s**
- Recovery (link restored → `CONNECTED` + fresh `pose`): **≤ 5 s**

### Autopilot adapters

An `AutopilotAdapter` interface isolates every per-autopilot difference:
- Mode mapping (PX4 `custom_mode` main/sub modes vs ArduPilot Copter mode numbers).
- Takeoff, goto and hold sequences. For example, ArduPilot needs GUIDED mode before takeoff and goto. The PX4 adapter uses `MAV_CMD_DO_REPOSITION` for goto.
- Stream-rate setup. Both use `MAV_CMD_SET_MESSAGE_INTERVAL` first. ArduPilot falls back to `MAVn_` (4.7) or `SRx_` (older) stream-rate parameters.
- Parameter value encoding (PX4 bytewise, ArduPilot float cast).
- Mission quirks (above).
- Signing provisioning: PX4 v1.17 vs v1.18 differences; `SETUP_SIGNING` only while disarmed on v1.18.
- Gimbal Manager v2 and Camera v2 capability detection.

Selection:
- The adapter is chosen from the `HEARTBEAT` autopilot field, and `AUTOPILOT_VERSION` refines the firmware version.
- Unknown autopilots get a `GenericMavlinkAdapter` that supports telemetry and only the command subset expressible through standard `MAV_CMD`s.
- Exact per-autopilot command sequences are verified against SITL in M2 and recorded in `docs/autopilots.md`.

### MAVLink 2 signing

- **Keys.** One 32-byte key per vehicle link, generated offline by `skyrelay-keys`. Stored `0600` under a configured directory, never committed (`secrets/` is git-ignored).
- **Replay protection.** Timestamp state is tracked per `(link, sysid, compid)` and persisted on shutdown and every 10 s, so restarts can't be replayed.
- **Unsigned allow-list.** Default `RADIO_STATUS` only. Everything else unsigned is dropped and counted.
- **Recordings.** `SETUP_SIGNING` messages are stripped before writing.
- **Wording.** The docs state plainly that signing authenticates and does not encrypt. WireGuard is the documented recommendation for encrypting IP links; it is not built in.

## 8. Security

- **DDS-Security**, on by default:
  - An offline OpenSSL CA, created by `skyrelay-keys`.
  - One identity per process role: `bridge`, `gateway`, `video`, `recorder`, `bench-probe`.
  - Governance: encrypt discovery and data, and reject unauthenticated participants.
  - Permissions:

    | Role | Writes | Reads |
    |---|---|---|
    | bridge | telemetry, `link_status`, `mission_state`, `payload_state`, `command_status` | `command_request` (both command partitions) |
    | gateway | `command_request` on `<fleet>.cmd` and `<fleet>.cmd.kill` | everything |
    | video | `footprint`, `video_stream_info` | `pose`, `payload_state` |
    | third-party app (template) | `command_request` on `<fleet>.cmd` only | everything |
    | recorder | — | everything |
    | bench-probe | `command_request` on `<fleet>.cmd`. A test-only permissions file adds `<fleet>.cmd.kill` | everything |
- **Command authority model.** DDS-Security permissions can restrict writes per partition but not per field, so authority is expressed through command partitions:
  - The bridge reads each command partition with its own DataReader, so it knows which partition every command arrived on.
  - It accepts `EMERGENCY_KILL` only from `<fleet>.cmd.kill`.
  - It checks `issuer_id` against `issuers.yaml`, which `skyrelay-keys` generates alongside the identities. That file maps each `issuer_id` to its identity's subject name and allowed partitions.
  - `issuer_id` is for **attribution, not authentication**. A holder of `<fleet>.cmd` write permission could put another issuer's ID in the field. The threat model states this; the real authority is the partition permission.
  - Operator roles (`viewer`, `operator`, `operator.kill`) exist only at the gateway. It maps them to which partition it writes on.
- **Gateway:**
  - TLS with a cert from the same offline CA.
  - Operator auth with pre-provisioned tokens. Hashed with Argon2id, kept in a config file, and mapped to roles `viewer`, `operator`, `operator.kill`.
  - Command authorisation is checked here. Every command is written to an audit log (an MCAP channel) with operator identity.
  - Browser connections are rate-limited.
- **Threat model.** `docs/threat-model.md` is a short STRIDE pass over links, DDS, gateway, browser and supply chain.
- **Secure-interop matrix.** `docs/interop.md` is generated by `bench/interop/`. It records Cyclone 11.1 ↔ Fast DDS 3.6.2, OpenDDS 3.34 and Connext 7.7 (non-commercial licence, local testing only), each with security off and on, using both the OMG `shape_main` binaries and SkyRelay's own IDL. Results are published whether they pass or fail.

## 9. Recording and replay

- **Format:** MCAP via the MCAP C++ library (header-only, MIT, 2.2.0). Recording can be switched on in the bridge, and a standalone `skyrelay-record` DDS participant is also provided.
- **Channels:**
  - `mavlink2`: a custom encoding of raw frames with link ID and rx `CLOCK_REALTIME` timestamp. MCAP has no registered MAVLink encoding; the format is documented in `docs/recording.md`.
  - One channel per DDS topic: encoding `cdr`, schema `omgidl`.
  - `audit`: gateway command log.
- **Replay.** `skyrelay-replay` feeds `mavlink2` frames into the bridge core with an injected clock, and no sockets or threads beyond the session. Because state machines are I/O-free and clock-injected, the output is deterministic.
- **Golden tests.** Committed short recordings (≤ 2 MB each) replay in CI. Produced DDS samples must match recorded ones field by field, except `t_bridge_ns`.
- **Export:** `.tlog` for use with existing MAVLink tools.
- **Cross-check:** eProsima DDS Record & Replay only, not a core component.

## 10. Performance targets and measurement

| Metric | Target | Measured from → to |
|---|---|---|
| Telemetry latency | p99 ≤ 100 ms | MAVLink frame captured on the **vehicle side** of the link emulator → DDS `pose` sample received by `bench-probe` |
| Command acknowledgement | p99 ≤ 1 s, and timeout rate ≤ 1 % | probe writes `command_request` → probe receives the first *definitive* status (`ACCEPTED`, `DENIED` or `FAILED`) derived from a `COMMAND_ACK`. `TIMED_OUT` is excluded from the latency distribution and counted in the timeout rate |
| Video latency | p99 ≤ 300 ms | frame capture time (KLV Precision Time Stamp, set from the simulator frame timestamp) → frame decoded by a headless probe player. The browser figure is reported separately as secondary |
| Lost-link detection | ≤ 1.5 s | blackout start → `link_status = LOST` at probe |
| Lost-link recovery | ≤ 5 s | link restored → `CONNECTED` + fresh `pose` at probe |
| Data reliability | ≥ 99 % | `pose` samples received by probe ÷ MAVLink position messages that entered the bridge, after decimation accounting |
| Isolation | other vehicles' telemetry p99 ≤ max(baseline p99 × 1.10, baseline p99 + 5 ms) | one vehicle on `radio-lossy`, others `clean`. Baseline and test values are each the median of 3 runs |

**Gated vs reported profiles.**
- The pass/fail targets above are **gated** on `clean`, `lan` and `wifi-good`. CI and releases fail if a gated target is missed.
- `wifi-poor` and `radio-lossy` are **stress profiles**. Every metric is still measured and published, but not gated, because end-to-end latency includes the emulated link's own delay. `radio-lossy` alone has 100 ± 30 ms one-way, which no bridge can beat.
- Reports show the profile's nominal one-way delay next to each result, so readers can see how much latency the stack itself adds.
- Lost-link detection and recovery are gated on `blackout-10s`.
- Benchmarks run with security **on** (the shipped default). Each report also includes a security-off column, to show the overhead.

**Clocks.** All containers on one host share the kernel's `CLOCK_REALTIME`, so single-host benchmarks need no clock sync. Multi-host runs require chrony or PTP, and the report records measured offset.

**Network profiles** (`bench/profiles/*.yaml`):
- Applied by a per-vehicle sidecar sharing the vehicle container's network namespace.
- netem only shapes egress, so both directions are shaped using an IFB device or a sidecar on each side. The mechanism is picked in M4 and recorded in an ADR.

| Profile | Delay | Jitter | Loss |
|---|---|---|---|
| `clean` | 0 | 0 | 0 |
| `lan` | 1 ms | 0.2 ms | 0 |
| `wifi-good` | 20 ms | 5 ms | 0.5 % |
| `wifi-poor` | 60 ms | 20 ms | 3 % |
| `radio-lossy` | 100 ms | 30 ms | 8 %, correlated 25 % |
| `blackout-10s` | clean, then 100 % loss for 10 s, then clean | | |

**One command:** `bench/run.sh <scenario>`.
- It brings up the sim, bridge and probes, runs a scripted flight (arm, takeoff, mission, commands, blackout), and tears down.
- It writes `bench/results/<date>-<scenario>/` with raw CSV, MCAP and `report.md` (tables + charts).
- Published results live in `docs/benchmarks/`.
- Simulation speed factor is 1 for latency runs.

## 11. Video and KLV

**Source.**
- PX4 Gazebo `x500_gimbal` (or `x500_mono_cam`) streams RTP H.264 on UDP `5600 + instance`.
- ArduPilot's camera plugin defaults every instance to port 5600, so the sim config overrides the port per instance.
- **Default:** 2 camera vehicles plus 2 without cameras, because of 4 GB VRAM. Raised to 4 only if measured headroom allows (M5 records it).

**`skyrelay-video` pipeline (per camera vehicle):**
1. Receive RTP H.264, depayload and parse, **without re-encoding** (latency and licence).
2. Each frame takes its capture timestamp from the RTP timestamp, mapped to wall clock via RTCP sender reports or the sim's frame time; the M5 spike settles which one is available.
3. Look up the DomainState at that time from a short history buffer of `pose` and `payload_state` (interpolated).
4. Compute the ground footprint: flat-earth intersection at home elevation in v1, with a DEM hook for later.
5. Build a ST 0601 Local Set (subset below), and insert an SEI user-data timestamp into the H.264 stream.
6. Publish to MediaMTX over RTSP with two tracks, H.264 and KLV (RFC 6597, `application/smpte336m`).
7. Publish `footprint` and `video_stream_info` on DDS.

**Encoding and licences.**
- `skyrelay-video` never encodes video. The H.264 comes from the source: the Gazebo camera plugin in simulation, or the camera itself on real hardware.
- The upstream sim camera plugins may encode with x264 (GPL). They run inside the simulation images, which are not part of the product (§3), so the default demo needs no NVIDIA encoder.
- M5 verifies which encoder the pinned plugin versions use. If a plugin can't be used as-is, the sim image switches its encoder to `openh264enc` (BSD), or to `nvh264enc` when the host has an NVIDIA GPU.

**Browser.**
- Video arrives via WHEP from MediaMTX ≥ 1.21.4, with KLV on the WebRTC data channel (MediaMTX ≥ 1.16).
- Frames are matched to metadata using `requestVideoFrameCallback` `rtpTimestamp` and the SEI timestamp. Never `captureTime`.

**`libskyrelay-klv`:**
- ST 0601.19 Local Set encode/decode:
  - BER-OID tags and BER lengths
  - 16-bit checksum (tag 1)
  - tags 2 (Precision Time Stamp), 5–7 (platform heading/pitch/roll), 13–15 (sensor lat/lon/true altitude), 16–17 (HFOV/VFOV), 18–20 (sensor relative azimuth/elevation/roll), 21 (slant range), 23–25 (frame centre lat/lon/elevation), 65 (LS version), 82–89 (corner lat/lon points 1–4)
- Validation:
  - Round-trip tests.
  - libFuzzer on the decoder.
  - CI cross-validation: our encoder output decoded by KWIVER `dump-klv` in a test-only container.
  - A small set of openly licensed sample `.ts` files generated by our own pipeline, published for others.
- Exact field encodings are taken from the standard text, which the repo links to and does not copy.

**Risk.** KLV muxing in GStreamer and FFmpeg has regressed silently between versions, so the first task of M5 is a timeboxed **spike** proving the RTSP-with-KLV → MediaMTX → WHEP path end-to-end. If the RTSP KLV track path fails, the fallback is MPEG-TS over SRT into MediaMTX. That fallback needs a pinned GStreamer version and a CI check that KLV survives.

## 12. Gateway and web station

**Gateway protocol.**
- JSON over WSS. Message types are generated from the IDL: a small Python codegen reads `idl/` and emits C++ `to_json`/`from_json` and TypeScript types. One source of truth; CI fails if generated files are stale.
- Server → client:
  - Telemetry is throttled per client (default `pose` 10 Hz), latest-value.
  - State topics are sent on change.
  - `command_status` is always forwarded.
- Client → server: `command_request` only. The gateway fills `issuer_id` from the authenticated operator and never trusts the client for it.

**Web station screens.** One page; the layout favours operator density over marketing gloss.

```
┌───────────────────────────────────────────────────────────────┬───────────────────┐
│  MAP (MapLibre, offline PMTiles)                              │ FLEET             │
│   ▲ v1  ▲ v2      ▲ v3                                        │ v1 ● CONN  PX4    │
│        ◇ footprint polygon     ── planned mission ──          │   82%  POSCTL     │
│                                                               │ v2 ● DEGR  ArduP  │
│                                                               │ v3 ○ LOST  PX4    │
├────────────────────────────┬──────────────────────────────────┼───────────────────┤
│ VIDEO v1 (+KLV overlay)    │ VIDEO v2                         │ COMMANDS (v1)     │
│                            │                                  │ [Arm][Takeoff 20m]│
│                            │                                  │ [Goto][RTH][Land] │
├────────────────────────────┴──────────────────────────────────┤ [Hover] [ABORT]   │
│ LATENCY HUD pose age p50 18ms p99 41ms · cmd p99 220ms · video│ cmd log: #12 ACK  │
└───────────────────────────────────────────────────────────────┴───────────────────┘
```

- **Latency HUD.**
  - Shows pose age (`now − t_bridge_ns`, computed at the gateway) plus a browser↔gateway RTT estimate.
  - Only `pose` counts: it is published as each position message is decoded, so its age is bridge + DDS + gateway, while `vehicle_state` and `health` are republished at a fixed rate from the last decoded message and the latched topics are replayed with the age of the stored sample.
  - It is labelled as indicative. Authoritative numbers come from `bench/`.
- **Offline maps.**
  - A regional Protomaps v4 PMTiles extract, with self-hosted glyphs and sprites, served by the gateway.
  - OSM attribution is shown on the map.
  - Satellite imagery is out of scope: no openly licensed, offline-redistributable source fits. Terrain tiles (Mapterhorn) are optional; their licence gets checked before they go in.
- **Demo region.** One configurable region; the default extract is ≤ 50 MB.

## 13. Simulation environments

Compose files live under `sim/`, all on Ubuntu 24.04 images, PX4 v1.17.0 (v1.18 tracked in CI once released), ArduPilot Copter 4.7.x and Gazebo Harmonic.

| Profile | Contents | Use |
|---|---|---|
| `sih-px4-4` | 4 × PX4 SIH, headless, no Gazebo | CI, fast iteration, scale tests (16+) |
| `sitl-ardupilot-4` | 4 × ArduPilot built-in SITL (`--count`) | CI, adapter tests |
| `gz-mixed-4` | Gazebo Harmonic server + 2 × PX4 `x500_gimbal` + 2 × ArduPilot (one with camera) | Demo, video, full benchmarks |
| `gz-px4-4` | Gazebo + 4 × PX4 | PX4-only benchmarks |

- One container per vehicle, so each has its own network namespace for the link-emulator sidecar.
- The PX4 vehicles after the first run with `PX4_GZ_STANDALONE=1`.
- `MAV_{i}_BROADCAST=1` (or an explicit remote address) is set so MAVLink leaves the container.
- GPU access: `--gpus all`, `NVIDIA_DRIVER_CAPABILITIES=compute,utility,graphics,display`, and `gz sim -s --headless-rendering` when no display is needed.
- Fault injection:
  - PX4: `SYS_FAILURE_EN` + `failure` / `MAV_CMD_INJECT_FAILURE` for GPS, battery, sensor and motor.
  - ArduPilot: `SIM_*` parameters.
  - Link faults: network profiles only, because PX4 rejects `mavlink_signal`.

## 14. Repository layout

```
skyrelay/
  idl/skyrelay/v1/*.idl
  lib/mavlink/          # libskyrelay-mavlink (links, framing, signing, microservices, adapters)
  lib/dds/              # libskyrelay-dds (generated types, QoS, mapping)
  lib/klv/              # libskyrelay-klv (standalone)
  apps/bridge/ apps/gateway/ apps/video/ apps/replay/ apps/keys/ apps/record/
  web/station/          # React + TS + Vite
  bench/                # run.sh, profiles/, probe/, interop/, report/
  sim/                  # compose files, models, worlds, sidecar images
  tools/codegen/        # IDL → JSON/TS codegen
  third_party/mavlink/  # pinned generated headers + PIN file with upstream commit
  tests/                # unit/, fuzz/, golden/ (recordings), integration/
  docs/ design/ adr/ research/ contract.md autopilots.md recording.md threat-model.md interop.md benchmarks/
  cmake/  CMakePresets.json  vcpkg.json
```

## 15. Build, test, release

- **Build:**
  - CMake ≥ 4.2 with presets (`dev`, `asan`, `tsan`, `release`) and Ninja.
  - Dependencies via a **vcpkg manifest** with a pinned baseline, plus overlay ports wherever vcpkg lags the pinned versions (Cyclone DDS 11.1.0, cyclonedds-cxx 11.1.0, MCAP 2.2.0).
  - Offline: vcpkg asset mirror plus a file binary cache under `third_party/cache/`. The cache is git-ignored and shipped in the release bundle.
  - MAVLink generated headers are vendored (committed) with a `PIN` file.
- **Tests:**
  - GoogleTest unit tests. Every state machine is tested with a fake clock and fake transport, including adversarial sequences: duplicate ACKs, out-of-order mission requests, seq wrap, signing replay attempts.
  - libFuzzer targets for the MAVLink parse+verify path and the KLV decoder (smoke run in CI, long runs locally).
  - Golden replay tests (§9).
  - Integration tests on `sih-px4-4` and `sitl-ardupilot-4`, asserting lifecycle correctness and the `clean`-profile targets.
  - ASan/UBSan and TSan CI jobs.
- **CI:** GitHub Actions.
  - Every push: build (26.04-equivalent and 24.04 images), unit, golden and fuzz smoke tests, codegen freshness, licence check.
  - Nightly or manual: SITL integration and the short benchmark.
- **Release bundles.** Two, both signed and each with its own SBOM.
  - **`skyrelay-runtime`:** the product.
    - CPack `.deb` packages and runtime OCI images (`docker save` tarballs).
    - The MediaMTX binary, the map extract and web station assets.
    - The vcpkg binary cache, plus sources for an offline rebuild.
    - A Syft CycloneDX SBOM.
    - Signatures with cosign using a local key, verified offline.
    - `install-offline.sh`.
    - No GPL components.
  - **`skyrelay-sim`:** optional, for demos and benchmarks.
    - Images for the `sih-px4-4` and `gz-mixed-4` profiles.
    - Built from public upstream Dockerfiles at pinned versions.
    - Includes `NOTICE`, the licence texts, and corresponding source archives for every GPL component (ArduPilot, x264, and so on).
- **Acceptance (G10):** on a host with networking disabled:
  - `skyrelay-runtime` builds from source and installs.
  - With `skyrelay-sim` loaded, the `sih-px4-4` demo runs end to end.
  - `gz-mixed-4` running offline from the sim bundle is verified once per release; it's documented, not gated in CI because of image size.

## 16. Milestones

Each milestone ends with a tagged release, a short demo recording, updated benchmark numbers, and ADRs for the decisions it made.

**Security comes before the published benchmarks** (M3 before M4), so every published number is measured on the secure default. Releases before M3 are tagged `v0.x.0-preview`:
- marked **INSECURE PREVIEW** in their release notes and in the web station banner;
- in insecure mode the gateway binds to loopback only, and the bridge refuses non-loopback DDS interfaces unless `--insecure-allow-network` is also given.

| # | Milestone | Delivers | Concepts exercised |
|---|---|---|---|
| M0 | Foundations | Repo layout, CMake presets, vcpkg, CI skeleton, `sih-px4-4` compose, IDL v1 draft, ADR-0001..n for the decisions in this doc | Build systems, reproducibility, containers |
| M1 | **Walking skeleton** (the two-minute demo) | 4 × PX4 SIH → bridge (telemetry only: `vehicle_info`, `vehicle_state`, `pose`, `health`, `link_status`) → DDS → gateway → web map with fleet list and latency HUD. `bench-probe` reports telemetry p50/p95/p99 on `clean` | MAVLink framing, heartbeats, message intervals, DDS topics/keys/QoS, WebSockets |
| M2 | Commands and missions | Full command lifecycle, mission upload, ArduPilot adapter, lost-link state machine, conformance checker, MCAP recording + deterministic replay + golden tests | Protocol state machines, idempotency, retries, determinism |
| M3 | Security | `skyrelay-keys`, MAVLink signing (PX4 v1.17/v1.18, ArduPilot), DDS-Security default-on with command partitions and `issuers.yaml`, gateway TLS/auth/audit, threat model, interop matrix | PKI, authentication vs encryption, access control |
| M4 | Fault-injection benchmark | Network profiles, sidecars, `bench/run.sh`, report generator with charts, isolation test, results published with security on plus a security-off overhead column | netem, measurement methodology, percentiles |
| M5 | Video + KLV | Spike first. Then `libskyrelay-klv`, `skyrelay-video`, MediaMTX, WHEP in the station, footprint overlay, video latency benchmark | Codecs, RTP, timestamps/sync, geometry |
| M6 | Release | Offline bundle, SBOM, signatures, docs pass, demo video, project write-up | Supply chain, packaging |

**Stretch, after v1:**
- HITL with a Pixhawk-class board running SIH on hardware (`SYS_HITL=2`).
- ROS 2 compatibility mode (`rt/` topics and ROS type naming).
- A non-MAVLink adapter, driven by a toy vendor simulator, to prove the adapter interface.
- A 16-vehicle SIH scale test.
- DEM-based footprint.

## 17. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Cyclone secure handshake fails with other vendors | Interop claim weakened | Test in M4 early. Publish results honestly. Fast DDS fallback documented |
| GStreamer/MediaMTX KLV path broken in pinned versions | M5 slips | Spike first, SRT/MPEG-TS fallback, CI guard |
| PX4 v1.18 lands mid-project with breaking signing/namespace changes | Adapter churn | Adapter isolates it. CI matrix covers v1.17 and v1.18 |
| 4 GB VRAM insufficient for 4 camera vehicles | Demo shows fewer cameras | 2-camera default. Measure and document |
| vcpkg ports lag pinned versions | Build friction | Overlay ports |
| Cyclone XTypes defaults / stale docs | Silent interop or type mismatch | Explicit annotations everywhere. XTypes evolution test in CI |
| Browser clock skew pollutes HUD numbers | Misleading demo numbers | HUD labelled indicative. Authoritative numbers from `bench/` only |
| Scope creep toward a ground station | Time sink | Non-goals list. Each UI feature must serve the demo or the contract |

## 18. Decisions from design review

Reviewing the first draft as if I had to build straight from it turned up six gaps. How each one was resolved:

1. **Command authority.**
   - Authority is per command partition (`<fleet>.cmd`, `<fleet>.cmd.kill`), enforced by DDS-Security permissions and checked again by the bridge.
   - `issuer_id` is static per identity and used for attribution only. `command_id` is a random 64-bit value.
   - The gateway rejects unauthorised browser requests locally, without touching DDS. (§6, §7, §8)
2. **Targets.**
   - Gates apply on the `clean`, `lan` and `wifi-good` profiles; the stress profiles are reported but not gated.
   - Command latency counts only definitive acknowledgements, and timeouts are a separate rate.
   - The isolation tolerance has an absolute floor of 5 ms and uses medians of 3 runs. (§10)
3. **Two writers on `payload_state`.** Resolved by splitting out the `footprint` topic and requiring one producer role per topic. (§6)
4. **What "identical samples" means.** Replaced by conformance:
   - a `valid_mask` field in every sample;
   - NaN for floating-point values that aren't available;
   - core and extended fields;
   - a conformance checker tool. (§2, §6)
5. **GPL and bundles.** The simulation environment is outside the product. There are two bundles, runtime (no GPL) and sim (with notices and sources), and `skyrelay-video` never encodes. (§3, §11, §15)
6. **When security arrives.** Security moves before benchmarks (M3 ↔ M4), and pre-M3 releases are loopback-only insecure previews. (§16)

## 19. Decided during implementation

These get decided while building, each recorded in an ADR:
- Exact per-autopilot command sequences (verified in SITL and documented).
- The two-way netem mechanism.
- Whether Cyclone XML QoS profiles are usable.
- How video frame wall-clock time is derived.
- Internal class and module boundaries inside each library.
- The web station's component structure and styling.
- Thread-safe queue implementation.
- Log format.
- Any default numeric value marked "configurable" above, as long as the targets in §10 still hold.
