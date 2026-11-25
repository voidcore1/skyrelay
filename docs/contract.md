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

In the field tables, bit is the `valid_mask` bit, and **core** fields are the
ones every supported autopilot has to supply. Everything else is **extended**:
valid when the vehicle has it.

### vehicle_info

| Bit | Field | Type | Unit | Class |
|---|---|---|---|---|
| 0 | `autopilot` | `AutopilotKind` | - | core |
| 1 | `firmware_version` | `string<32>` | - | extended |
| 2 | `vehicle_type` | `VehicleType` | - | core |
| 3 | `mav_sysid` | `octet` | - | core |
| 4 | `mav_compid` | `octet` | - | core |
| 5 | `capabilities` | `uint64` | - | extended |
| 6 | `contract_version` | `string<16>` | - | core |

### vehicle_state

| Bit | Field | Type | Unit | Class |
|---|---|---|---|---|
| 0 | `armed` | `boolean` | - | core |
| 1 | `mode` | `FlightMode` | - | core |
| 2 | `mode_raw` | `string<32>` | - | core |
| 3 | `landed` | `LandedState` | - | extended |
| 4 | `system_status` | `octet` | - | extended |

### pose

| Bit | Field | Type | Unit | Class |
|---|---|---|---|---|
| 0 | `lat_deg` | `double` | deg | core |
| 1 | `lon_deg` | `double` | deg | core |
| 2 | `alt_msl_m` | `double` | m | core |
| 3 | `alt_rel_home_m` | `double` | m | extended |
| 4 | `q_w` | `float` | - | core |
| 5 | `q_x` | `float` | - | core |
| 6 | `q_y` | `float` | - | core |
| 7 | `q_z` | `float` | - | core |
| 8 | `vel_n_mps` | `float` | m/s | core |
| 9 | `vel_e_mps` | `float` | m/s | core |
| 10 | `vel_d_mps` | `float` | m/s | core |
| 11 | `heading_deg` | `float` | deg | extended |

### health

| Bit | Field | Type | Unit | Class |
|---|---|---|---|---|
| 0 | `battery_voltage_v` | `float` | V | core |
| 1 | `battery_current_a` | `float` | A | extended |
| 2 | `battery_remaining_pct` | `float` | % | core |
| 3 | `battery_time_remaining_s` | `float` | s | extended |
| 4 | `gnss_fix` | `GnssFix` | - | core |
| 5 | `gnss_satellites` | `octet` | - | core |
| 6 | `gnss_hdop` | `float` | - | extended |
| 7 | `sensors_present` | `uint32` | - | extended |
| 8 | `sensors_enabled` | `uint32` | - | extended |
| 9 | `sensors_healthy` | `uint32` | - | extended |
| 10 | `estimator_ok` | `boolean` | - | extended |

### link_status

| Bit | Field | Type | Unit | Class |
|---|---|---|---|---|
| 0 | `state` | `LinkState` | - | core |
| 1 | `state_since_ns` | `uint64` | ns | core |
| 2 | `rx_rate_hz` | `float` | Hz | core |
| 3 | `loss_pct` | `float` | % | core |
| 4 | `last_rx_age_ms` | `uint32` | ms | core |
| 5 | `signing` | `SigningState` | - | core |

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
