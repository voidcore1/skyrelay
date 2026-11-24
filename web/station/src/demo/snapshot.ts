import { HealthValid, LinkStatusValid, PoseValid, VehicleInfoValid, VehicleStateValid } from '../contract/v1'
import type { LinkState } from '../contract/v1'
import type { SampleMsg } from '../net/protocol'
import { applySample, type FleetState } from '../state/fleetStore'
import { countsForLatency, LatencyWindow } from '../state/latency'

export const SCHEMATIC_BOUNDS = { west: -0.004, east: 0.004, north: 0.003, south: -0.003 }

const vehicles = [
  { id: 1, lon: -0.0019, lat: 0.0012, heading: 65, altitude: 42, battery: 83, rx: 24, age: 12, link: 'LINK_CONNECTED' },
  { id: 2, lon: 0.0010, lat: 0.0013, heading: 210, altitude: 31, battery: 56, rx: 7, age: 24, link: 'LINK_DEGRADED' },
  { id: 3, lon: 0.0005, lat: -0.0015, heading: 315, altitude: 18, battery: 32, rx: 0, age: 86, link: 'LINK_LOST' },
] as const satisfies ReadonlyArray<{
  id: number
  lon: number
  lat: number
  heading: number
  altitude: number
  battery: number
  rx: number
  age: number
  link: LinkState
}>

const samples = vehicles.flatMap<SampleMsg>((v) => {
  const fields = { vehicle_id: v.id }
  const header = (valid_mask: number) => ({ seq: 1, t_source_us: 0, t_bridge_ns: 0, valid_mask })
  return [
    {
      type: 'sample', topic: 'vehicle_info', age_ms: null,
      data: {
        ...fields, header: header(VehicleInfoValid.autopilot), autopilot: v.id === 2 ? 'AUTOPILOT_ARDUPILOT' : 'AUTOPILOT_PX4',
        firmware_version: '', vehicle_type: 'VEHICLE_TYPE_UNKNOWN', mav_sysid: 0, mav_compid: 0,
        capabilities: 0, contract_version: '',
      },
    },
    {
      type: 'sample', topic: 'vehicle_state', age_ms: null,
      data: {
        ...fields, header: header(VehicleStateValid.armed | VehicleStateValid.mode | VehicleStateValid.landed),
        armed: true, mode: v.id === 1 ? 'FLIGHT_MODE_MISSION' : 'FLIGHT_MODE_HOLD', mode_raw: '',
        landed: 'LANDED_STATE_IN_AIR', system_status: 0,
      },
    },
    {
      type: 'sample', topic: 'health', age_ms: null,
      data: {
        ...fields, header: header(HealthValid.battery_remaining_pct), battery_remaining_pct: v.battery,
        battery_voltage_v: null, battery_current_a: null, battery_time_remaining_s: null,
        gnss_fix: 'GNSS_FIX_UNKNOWN', gnss_satellites: 0, gnss_hdop: null, sensors_present: 0,
        sensors_enabled: 0, sensors_healthy: 0, estimator_ok: false,
      },
    },
    {
      type: 'sample', topic: 'link_status', age_ms: null,
      data: {
        ...fields, header: header(LinkStatusValid.state | LinkStatusValid.rx_rate_hz), state: v.link, rx_rate_hz: v.rx,
        state_since_ns: 0, loss_pct: null, last_rx_age_ms: 0, signing: 'SIGNING_OFF',
      },
    },
    {
      type: 'sample', topic: 'pose', age_ms: v.age,
      data: {
        ...fields, header: header(PoseValid.lat_deg | PoseValid.lon_deg | PoseValid.alt_rel_home_m | PoseValid.heading_deg),
        lat_deg: v.lat, lon_deg: v.lon, alt_rel_home_m: v.altitude, heading_deg: v.heading,
        alt_msl_m: null, q_w: null, q_x: null, q_y: null, q_z: null, vel_n_mps: null, vel_e_mps: null, vel_d_mps: null,
      },
    },
  ]
})

const replayVehicle = vehicles[0]
export const REPLAY_VEHICLE_ID = replayVehicle.id
export const REPLAY_SECONDS = 12

const connected = { link: replayVehicle.link, rx: replayVehicle.rx, ageMs: replayVehicle.age }
const replayPhases = [
  { second: 0, ...connected },
  { second: 3, link: 'LINK_DEGRADED', rx: 7, ageMs: 72 },
  { second: 5, link: 'LINK_LOST', rx: 0, ageMs: null },
  { second: 8, link: 'LINK_RECOVERING', rx: 12, ageMs: 44 },
  { second: 10, ...connected },
] as const satisfies ReadonlyArray<{ second: number; link: LinkState; rx: number; ageMs: number | null }>

// Build the frames once on the replay clock. Skipped renders then have no
// effect on the fleet state or which samples make it into the HUD window.
export function demoReplayFrames() {
  let fleet: FleetState = {}
  const ages = new LatencyWindow()
  function apply(sample: SampleMsg, nowMs: number) {
    fleet = applySample(fleet, sample)
    if (countsForLatency(sample)) ages.add(sample.age_ms, nowMs)
  }
  for (const sample of samples) apply(sample, 0)

  const frames = [{ fleet, latency: ages.stats(0) }]
  const initial = fleet[REPLAY_VEHICLE_ID]!
  let phase: (typeof replayPhases)[number] = replayPhases[0]
  let posesReceived = 0

  for (let second = 1; second <= REPLAY_SECONDS; second++) {
    const nowMs = second * 1000
    const nextPhase = replayPhases.find((entry) => entry.second === second)
    if (nextPhase) {
      phase = nextPhase
      const link = initial.link!
      apply({
        type: 'sample', topic: 'link_status', age_ms: null,
        data: {
          ...link, header: { ...link.header, seq: second + 1 },
          state: phase.link, rx_rate_hz: phase.rx, state_since_ns: nowMs * 1_000_000,
        },
      }, nowMs)
    }
    if (phase.ageMs !== null) {
      posesReceived++
      const pose = initial.pose!
      apply({
        type: 'sample', topic: 'pose', age_ms: phase.ageMs,
        data: {
          ...pose, header: { ...pose.header, seq: second + 1 },
          lon_deg: replayVehicle.lon + posesReceived * 0.000012,
          lat_deg: replayVehicle.lat + posesReceived * 0.000008,
        },
      }, nowMs)
    }
    frames.push({ fleet, latency: ages.stats(nowMs) })
  }
  return frames
}
