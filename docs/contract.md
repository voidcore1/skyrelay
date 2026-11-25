# SkyRelay data contract, IDL v1

This is the contract: the five telemetry topics the bridge publishes, plus the
command and mission topics added in M2. The types are defined in `idl/skyrelay/v1/types.idl`, and where this
page and the IDL disagree, the IDL is right. The reasons for the rules are in
[ADR-0006](adr/0006-idl-rules.md).

**INSECURE PREVIEW.** There is no authentication or encryption in M1. In the
default configuration DDS sends and advertises only on the loopback interface,
but Cyclone DDS binds its receive sockets to every interface, so another host
that can reach UDP 17910–18039 (domain 42) could inject samples. Block those
ports to other hosts with a host firewall, or use a trusted network, until
DDS-Security arrives in M3.

## Conventions

- Module `skyrelay::v1`, topic names `skyrelay/v1/<name>`, DDS domain 42 and
  data partition `default` unless configured otherwise.
- Wire format XCDR2. Every struct is `@appendable`, and fields and enum values
  are only ever appended.
- Units are SI. Positions are WGS-84 degrees as `double`. Altitude names say
  their reference (`alt_msl_m`, `alt_rel_home_m`). Velocities are NED.
  Attitude is a quaternion in (w, x, y, z) order.
- Every topic has one producer role, so `seq` stays monotonic and samples from
  two writers never overwrite each other. `command_request` is the exception:
  it has several producers, and its key includes `issuer_id` so their
  instances never collide.
- A 64-bit identifier (`// unit: id` in the IDL) is a decimal string in JSON
  and a `string` in TypeScript. Every other integer is a JSON number.

## Header

Every sample starts with `@key uint32 vehicle_id` and a `Header`:

| Field | Meaning |
|---|---|
| `seq` | Per vehicle per topic, monotonic. |
| `t_source_us` | Vehicle time in UTC, in microseconds. 0 if unknown. |
| `t_bridge_ns` | `CLOCK_REALTIME` in nanoseconds when the bridge decoded the source message. |
| `valid_mask` | One bit per field after `header`. A cleared bit means the value is unavailable. |

### valid_mask

Bit *i* belongs to the *i*-th field after `header`, counting from 0 in the order
they appear below. A cleared bit means the vehicle could not supply the value.
In that case a floating-point field is also NaN, so a reader that ignores the
mask fails loudly, and an integer, boolean or enum field is 0 (strings are
empty).

## QoS profiles

| Profile | Reliability | History | Durability | Other |
|---|---|---|---|---|
| `telemetry_fast` | RELIABLE | KEEP_LAST 1 | VOLATILE | deadline 200 ms, liveliness AUTOMATIC, lease 1 s |
| `telemetry_reliable` | RELIABLE | KEEP_LAST 1 | VOLATILE | deadline 1 s |
| `state_latched` | RELIABLE | KEEP_LAST 1 | TRANSIENT_LOCAL | none |
| `command` | RELIABLE | KEEP_ALL, 64 samples per instance | VOLATILE | lifespan 5 s |
| `command_status` | RELIABLE | KEEP_LAST 1 per instance | TRANSIENT_LOCAL | lifespan 60 s |

Writers, readers and topics use the same profile, built in
`lib/dds/src/qos.cpp`.

## Topics

| Topic | Profile | Default rate |
|---|---|---|
| `skyrelay/v1/vehicle_info` | `state_latched` | on change |
| `skyrelay/v1/vehicle_state` | `telemetry_reliable` | 5 Hz + on change |
| `skyrelay/v1/pose` | `telemetry_fast` | 20 Hz |
| `skyrelay/v1/health` | `telemetry_reliable` | 2 Hz + on change |
| `skyrelay/v1/link_status` | `state_latched` | on change + 1 Hz |
| `skyrelay/v1/mission_state` | `state_latched` | on change |
| `skyrelay/v1/command_request` | `command` | per command |
| `skyrelay/v1/command_status` | `command_status` | one per state entered |

The field tables are generated from the IDL into
[`contract/v1-fields.md`](contract/v1-fields.md), so the bit numbers and the
core/extended class live in one place. Bit is the `valid_mask` bit, and **core**
fields are the ones every supported autopilot has to supply (marked `, core` in
the IDL). Everything else is **extended**: valid when the vehicle has it.

### vehicle_info

The fields, bits, units and core/extended class are in the [VehicleInfo table](contract/v1-fields.md#vehicleinfo), generated from the IDL.

### vehicle_state

The fields, bits, units and core/extended class are in the [VehicleState table](contract/v1-fields.md#vehiclestate), generated from the IDL.

### pose

The fields, bits, units and core/extended class are in the [Pose table](contract/v1-fields.md#pose), generated from the IDL.

### health

The fields, bits, units and core/extended class are in the [Health table](contract/v1-fields.md#health), generated from the IDL.

### link_status

The fields, bits, units and core/extended class are in the [LinkStatus table](contract/v1-fields.md#linkstatus), generated from the IDL.

`last_rx_age_ms` is the time since the bridge last received a valid frame from
the vehicle. It is `4294967295` (`UINT32_MAX`) when nothing has been received
yet, and for any age of 49.7 days or more; the field is valid in both cases.

## Commands and missions

### Partitions

The data partition `<fleet>` (default `default`) carries telemetry,
`mission_state` and `command_status`. `command_request` is written on
`<fleet>.cmd`, or on `<fleet>.cmd.kill` for `EMERGENCY_KILL`. The partition a
writer uses is the authority to send: a participant that is not in
`<fleet>.cmd` cannot reach the bridge's command reader at all, and one that is
not in `<fleet>.cmd.kill` cannot send a kill. `command_partition(fleet)` and
`kill_partition(fleet)` in `skyrelay/dds/topics.hpp` build the names.

### Keys and ids

A command is identified by `(vehicle_id, issuer_id, command_id)`, and
`command_status` carries the same key, so a status always lands on the instance
of its request.

- `command_id` is a random `uint64` from the system CSPRNG (`getrandom(2)`),
  never 0. It is random rather than a counter so two issuers, or one issuer
  after a restart, cannot collide.
- `issuer_id` is a static `uint32`, never 0, chosen by whoever runs the issuer.
  The gateway is 1 (`--issuer-id`), the bench probe 2 and the conformance
  checker 3; third parties should use 100 and up. In M2 it is attribution only,
  not authentication.
- `confirm_token` is 0 except for `EMERGENCY_KILL`.

### Header of a command_request

`header.seq` counts per issuer and vehicle. `header.t_bridge_ns` is the
issuer's `CLOCK_REALTIME` when it writes the sample (not a time the bridge
stamps), and `header.t_source_us` is 0. `valid_mask` follows the usual rule.
An issuer sets every bit of `valid_mask` (`valid::CommandRequest::kAll`): a
request has no unavailable fields, only fields a kind does not read, which are
NaN or 0.

### command_request

Fields, bits and units are in the
[CommandRequest table](contract/v1-fields.md#commandrequest).
`waypoints` holds at most 500 items and is used only by `MISSION_UPLOAD`.
`deadline_ms` is 0 for the default (1000 ms) or 100 to 60000. `operator_name`
is free text for the audit trail (at most 64 bytes); it is not
called `operator` because that is a C++ keyword.

### CommandParams by kind

A field a kind does not read is NaN. For `GOTO`, a NaN `ground_speed_mps` or
`heading_deg` means keep the current value. 0 is always a value, never
"unset".

| Kind | Reads |
|---|---|
| `ARM`, `DISARM`, `LAND`, `RETURN_HOME`, `HOLD` | nothing |
| `TAKEOFF` | `alt_rel_home_m` |
| `GOTO` | `lat_deg`, `lon_deg`, `alt_rel_home_m`, `ground_speed_mps` (NaN keeps), `heading_deg` (NaN keeps) |
| `SET_SPEED` | `ground_speed_mps` |
| `SET_ALTITUDE` | `alt_rel_home_m` |
| `SET_HEADING` | `heading_deg` |
| `MISSION_UPLOAD` | `waypoints` (not `params`) |
| `MISSION_START`, `MISSION_PAUSE`, `MISSION_CLEAR` | nothing |
| `GIMBAL_POINT` | `gimbal_pitch_deg`, `gimbal_yaw_deg` |
| `CAMERA_ZOOM` | `zoom_level` |
| `CAMERA_CAPTURE`, `VIDEO_START`, `VIDEO_STOP` | nothing |
| `EMERGENCY_ABORT`, `EMERGENCY_HOVER` | nothing |
| `EMERGENCY_KILL` | nothing; needs `confirm_token` |

### Validation

A request is checked before anything goes to the vehicle, and a failure is a
`REJECTED_*` status with the reason in `detail`. The checks run in this order:

1. `issuer_id` and `command_id` are non-zero, else `REJECTED_INVALID`.
2. A kind this build does not know is `REJECTED_UNSUPPORTED`; `UNKNOWN` is
   `REJECTED_INVALID`.
3. `EMERGENCY_KILL` outside `<fleet>.cmd.kill` is `REJECTED_UNAUTHORISED`, any
   other kind on the kill partition is `REJECTED_INVALID`, and a kill with the
   wrong `confirm_token` is `REJECTED_INVALID`.
4. `deadline_ms` is 0 (default, 1000 ms) or 100 to 60000.
5. `waypoints` is empty for every kind except `MISSION_UPLOAD`.
6. The fields the kind reads are in range:

| Kind | Field | Accepted |
|---|---|---|
| `TAKEOFF`, `SET_ALTITUDE` | `alt_rel_home_m` | 1 to 500 |
| `GOTO` | `lat_deg` | -90 to 90 |
| | `lon_deg` | -180 to 180 |
| | `alt_rel_home_m` | 1 to 500 |
| | `ground_speed_mps` | NaN, or above 0 up to 30 |
| | `heading_deg` | NaN, or 0 up to (not including) 360 |
| `SET_SPEED` | `ground_speed_mps` | above 0 up to 30 |
| `SET_HEADING` | `heading_deg` | 0 up to (not including) 360 |
| `MISSION_UPLOAD` | `waypoints` | 1 to 500 items |
| | waypoint `lat_deg`, `lon_deg` | as for `GOTO` |
| | waypoint `alt_rel_home_m` | 1 to 500 |
| | waypoint `hold_s` | NaN, or 0 to 3600 |
| | waypoint `acceptance_radius_m` | NaN, or above 0 up to 1000 |

Only the kinds in this table have ranged fields. For them, NaN is out of
range except where the table allows it, and fields a kind does not read are
not checked. The gimbal and camera kinds are `REJECTED_UNSUPPORTED` until M5,
so their fields have no ranges yet. A range failure reads
`<field> out of range: <value> (allowed <min>-<max>)`, with `waypoints[<i>].`
in front for a waypoint field.

Every number in this section is a named constant in
`lib/mavlink/include/skyrelay/vehicle/command_validation.hpp`, and a unit test
reads this table back and compares it with them.

### Deadlines, retries and busy MAVLink commands

A kind can take more than one MAVLink command; on some autopilots, for
example, a mode change goes out before the takeoff. The bridge sends them one
at a time, each after the one before it was accepted, and publishes only the
request's own states: one `SENT`, any `IN_PROGRESS`, one terminal status.

- `deadline_ms` applies to each MAVLink command of the request, counted from
  its first send. Each is resent at 350 ms and 700 ms if no ack has arrived.
- An `IN_PROGRESS` ack stops the resends and extends that command's deadline
  to 10 s after its first send (or `deadline_ms`, if that is longer).
  `IN_PROGRESS` is published again only when `progress_pct` changes.
- `EMERGENCY_ABORT` has a fallback: if the vehicle denies or fails the return,
  the bridge sends a land, and the final status's `detail` says so, for
  example `MAV_CMD 20 denied (MAV_RESULT 2), sent fallback MAV_CMD 21`.
- A request is `REJECTED_BUSY` if any MAVLink command it would send is in
  flight for another request on the same vehicle. A command the vehicle may
  still answer is held back as well. If it timed out (after `IN_PROGRESS` or
  not), was cancelled by a shutdown, or had to be resent, which the vehicle
  answers once per copy, it stays busy for 700 ms after it ends, so a late
  ack is not taken for the next request. The `detail` says which:
  `MAV_CMD 192 ended without an ack; waiting 700 ms for late acks` or
  `MAV_CMD 176 was retried; waiting 700 ms for late acks`.

### command_status and the lifecycle

Fields are in the
[CommandStatus table](contract/v1-fields.md#commandstatus). The bridge writes
one sample for each state a command enters, so a late reader gets the latest
state of each command and a recording holds the whole history.

```
RECEIVED -> REJECTED_INVALID | REJECTED_UNSUPPORTED | REJECTED_BUSY | REJECTED_UNAUTHORISED
RECEIVED -> SENT -> [IN_PROGRESS]* -> ACCEPTED | DENIED | FAILED | TIMED_OUT | CANCELLED
```

| State | Meaning |
|---|---|
| `RECEIVED` | The bridge read the request and queued it. |
| `SENT` | The first MAVLink message went out. |
| `IN_PROGRESS` | The vehicle answered `IN_PROGRESS` or a mission transfer is under way; `progress_pct` says how far. |
| `ACCEPTED` | The vehicle answered `MAV_RESULT_ACCEPTED`. This does not mean the manoeuvre has finished. |
| `DENIED` | The vehicle refused: `DENIED`, `TEMPORARILY_REJECTED`, `UNSUPPORTED`, `COMMAND_LONG_ONLY`, `COMMAND_INT_ONLY` or `COMMAND_UNSUPPORTED_MAV_FRAME`. `mav_result` has the value. |
| `FAILED` | The vehicle answered `FAILED` or a result the bridge does not know, or a mission transfer failed. |
| `TIMED_OUT` | No definitive answer by the deadline; `detail` names the MAVLink command. |
| `CANCELLED` | The vehicle answered `MAV_RESULT_CANCELLED`, or the bridge shut down while the command was in flight (`detail` says so). |
| `REJECTED_INVALID` | A field is out of range, or the kill token is wrong. |
| `REJECTED_UNSUPPORTED` | The kind is not available for this vehicle or autopilot. |
| `REJECTED_BUSY` | A MAVLink command the request needs is in flight for another request, or ended less than 700 ms ago without an ack of its own or after being resent, or the inbox is full. |
| `REJECTED_UNAUTHORISED` | `EMERGENCY_KILL` arrived outside `<fleet>.cmd.kill`. |

Every `REJECTED_*`, `ACCEPTED`, `DENIED`, `FAILED`, `TIMED_OUT` and
`CANCELLED` is terminal.

Timestamps are `CLOCK_REALTIME` nanoseconds. `t_received_ns` is set on every
status, `t_sent_ns` from `SENT` on, `t_in_progress_ns` records the first
`IN_PROGRESS`, and `t_terminal_ns` is set on every terminal status, rejections
included, and on no other. A cleared bit means the transition has not
happened. `mav_result` is the last `COMMAND_ACK` result for the MAVLink command
in flight, and `attempts` counts the MAVLink sends of the whole request,
retries included.

### The kill rule

`EMERGENCY_KILL` is accepted only from `<fleet>.cmd.kill`; from anywhere else it
is `REJECTED_UNAUTHORISED`. It also needs
`confirm_token == command_id XOR vehicle_id`, else `REJECTED_INVALID`. This is
a guard against sending it by accident, not a security control. The gateway
refuses `EMERGENCY_KILL` from browsers, and the station has no kill button.

### mission_state

Fields are in the [MissionState table](contract/v1-fields.md#missionstate).
`mission_state` has no core fields in v1: what autopilots report about a
mission varies, so every field is extended and valid only when the vehicle
supplied it. `mission_hash` is FNV-1a 64 of what this bridge uploaded, so a
reader can tell whether the vehicle holds that plan.

## Enums

The first literal of every enum is the unknown (zero) value. The order mirrors
the bridge's internal enums and is append-only.

| Enum | Literals, in order |
|---|---|
| `AutopilotKind` | `AUTOPILOT_UNKNOWN`, `_PX4`, `_ARDUPILOT`, `_OTHER` |
| `VehicleType` | `VEHICLE_TYPE_UNKNOWN`, `_MULTIROTOR`, `_FIXED_WING`, `_VTOL`, `_OTHER` |
| `FlightMode` | `FLIGHT_MODE_UNKNOWN`, `_MANUAL`, `_STABILIZED`, `_ACRO`, `_ALTITUDE_HOLD`, `_POSITION_HOLD`, `_HOLD`, `_MISSION`, `_RETURN`, `_LAND`, `_TAKEOFF`, `_GUIDED`, `_OFFBOARD` |
| `LandedState` | `LANDED_STATE_UNKNOWN`, `_ON_GROUND`, `_IN_AIR`, `_TAKING_OFF`, `_LANDING` |
| `GnssFix` | `GNSS_FIX_UNKNOWN`, `_NO_FIX`, `_2D`, `_3D`, `_DGPS`, `_RTK_FLOAT`, `_RTK_FIXED` |
| `LinkState` | `LINK_LOST`, `LINK_RECOVERING`, `LINK_CONNECTED`, `LINK_DEGRADED` |
| `CommandKind` | `COMMAND_KIND_UNKNOWN`, then one literal per kind in the table above |
| `CommandState` | `COMMAND_STATE_UNKNOWN`, then the states in the lifecycle table |
| `MissionRunState` | `MISSION_RUN_UNKNOWN`, `_NONE`, `_NOT_STARTED`, `_ACTIVE`, `_PAUSED`, `_COMPLETE` (MAVLink `MISSION_STATE` 0 to 5) |
| `SigningState` | `SIGNING_OFF`, `SIGNING_ON`, `SIGNING_FAILED` |

`LinkState` starts at `LINK_LOST` because a link we know nothing about is a
lost link. `VehicleType`, `FlightMode` and the other enums start at unknown.
