# SkyRelay data contract, IDL v1

This is the M1 part of the contract: the five telemetry topics that the bridge
publishes. The types are defined in `idl/skyrelay/v1/types.idl`, and where this
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
  two writers never overwrite each other.

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
| `SigningState` | `SIGNING_OFF`, `SIGNING_ON`, `SIGNING_FAILED` |

`LinkState` starts at `LINK_LOST` because a link we know nothing about is a
lost link. `VehicleType`, `FlightMode` and the other enums start at unknown.
