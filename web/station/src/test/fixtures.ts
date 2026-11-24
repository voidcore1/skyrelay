// Sample builders shared by the unit tests.
import type { Header, Health, LinkState, LinkStatus, Pose, VehicleInfo, VehicleState } from '../contract/v1'
import type { SampleMsg } from '../net/protocol'

export const header: Header = { seq: 1, t_source_us: 0, t_bridge_ns: 1_795_400_000_000_000_000, valid_mask: 0 }

export const pose: Pose = {
  vehicle_id: 1,
  header,
  lat_deg: 47.397742,
  lon_deg: 8.545594,
  alt_msl_m: 488.2,
  alt_rel_home_m: 0,
  q_w: 1,
  q_x: 0,
  q_y: 0,
  q_z: 0,
  vel_n_mps: 0,
  vel_e_mps: 0,
  vel_d_mps: 0,
  heading_deg: 90,
}

export const info: VehicleInfo = {
  vehicle_id: 1,
  header,
  autopilot: 'AUTOPILOT_PX4',
  firmware_version: '1.17.0',
  vehicle_type: 'VEHICLE_TYPE_MULTIROTOR',
  mav_sysid: 1,
  mav_compid: 1,
  capabilities: 0,
  contract_version: '1.0.0-preview.1',
}

export const vehicleState: VehicleState = {
  vehicle_id: 1,
  header,
  armed: false,
  mode: 'FLIGHT_MODE_HOLD',
  mode_raw: 'AUTO.LOITER',
  landed: 'LANDED_STATE_ON_GROUND',
  system_status: 3,
}

export const health: Health = {
  vehicle_id: 1,
  header,
  battery_voltage_v: 16.2,
  battery_current_a: -1,
  battery_remaining_pct: 100,
  battery_time_remaining_s: null,
  gnss_fix: 'GNSS_FIX_3D',
  gnss_satellites: 10,
  gnss_hdop: 0.7,
  sensors_present: 0,
  sensors_enabled: 0,
  sensors_healthy: 0,
  estimator_ok: true,
}

export const linkStatus: LinkStatus = {
  vehicle_id: 1,
  header,
  state: 'LINK_CONNECTED',
  state_since_ns: 0,
  rx_rate_hz: 360,
  loss_pct: 0,
  last_rx_age_ms: 5,
  signing: 'SIGNING_OFF',
}

export function poseMsg(id: number, lat: number | null, lon: number | null, ageMs: number | null = 1): SampleMsg {
  return { type: 'sample', topic: 'pose', data: { ...pose, vehicle_id: id, lat_deg: lat, lon_deg: lon }, age_ms: ageMs }
}

export function linkMsg(id: number, state: LinkState, ageMs: number | null = 1): SampleMsg {
  return { type: 'sample', topic: 'link_status', data: { ...linkStatus, vehicle_id: id, state }, age_ms: ageMs }
}
