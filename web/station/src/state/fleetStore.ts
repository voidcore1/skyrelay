// What the station knows about each vehicle: the latest sample of every topic.
// The reducers are pure, so React can tell what changed by identity: a message
// replaces only the vehicle it is about.
//
// The bridge publishes link_status for every vehicle at least once a second,
// so a vehicle whose last link_status is more than STALE_AFTER_MS old, or whose
// link_status the gateway has reported gone, is shown as STALE: its link state
// and everything else about it are no longer current.
import { LinkStatusValid, type Health, type LinkStatus, type Pose, type VehicleInfo, type VehicleState } from '../contract/v1'
import type { GoneMsg, SampleMsg } from '../net/protocol'
import { linkLabel, type LinkLabel } from './link'
import { isValid } from './valid'

export const STALE_AFTER_MS = 3000

export interface VehicleView {
  id: number
  info?: VehicleInfo
  state?: VehicleState
  pose?: Pose
  health?: Health
  link?: LinkStatus
  // age_ms of the most recent sample that had one.
  lastAgeMs?: number
  // When the last link_status was current, on the station's clock in ms
  // (performance.now()): its arrival time minus its age, so a replayed sample
  // counts from when the bridge sent it. Unset without a clock (the demo).
  linkCurrentAt?: number
  // The gateway said this vehicle's link_status has no writer any more.
  gone?: boolean
}

export type FleetState = Readonly<Record<number, VehicleView>>

const slotOf = {
  vehicle_info: 'info',
  vehicle_state: 'state',
  pose: 'pose',
  health: 'health',
  link_status: 'link',
} as const satisfies Record<SampleMsg['topic'], keyof VehicleView>

export function applySample(s: FleetState, msg: SampleMsg): FleetState {
  const slot = Object.hasOwn(slotOf, msg.topic) ? slotOf[msg.topic] : undefined
  if (slot === undefined) return s // a topic from a newer contract
  const id = msg.data.vehicle_id
  const prev = s[id]
  const next: VehicleView = { ...prev, id, [slot]: msg.data }
  if (msg.age_ms !== null) next.lastAgeMs = msg.age_ms
  return { ...s, [id]: next }
}

// applySample for a sample that arrived at receivedAtMs (performance.now()),
// which also keeps track of how current the vehicle's link_status is.
export function receiveSample(s: FleetState, msg: SampleMsg, receivedAtMs: number): FleetState {
  const next = applySample(s, msg)
  if (next === s || msg.topic !== 'link_status') return next
  const id = msg.data.vehicle_id
  const age = msg.age_ms !== null && msg.age_ms > 0 ? msg.age_ms : 0
  return { ...next, [id]: { ...next[id]!, linkCurrentAt: receivedAtMs - age, gone: false } }
}

// A gone notice for link_status marks the vehicle stale at once. The data is
// kept, so the last position stays on the map, dimmed.
export function applyGone(s: FleetState, msg: GoneMsg): FleetState {
  const prev = s[msg.vehicle_id]
  if (msg.topic !== 'link_status' || prev === undefined || prev.gone) return s
  return { ...s, [msg.vehicle_id]: { ...prev, gone: true } }
}

export function isStale(v: VehicleView, nowMs: number | undefined): boolean {
  if (v.gone) return true
  return nowMs !== undefined && v.linkCurrentAt !== undefined && nowMs - v.linkCurrentAt > STALE_AFTER_MS
}

// The link label to show: STALE when the vehicle is stale at nowMs, else its
// last reported link state (UNKNOWN without one). Without nowMs only a gone
// notice makes it stale.
export function vehicleLink(v: VehicleView, nowMs?: number): LinkLabel {
  if (isStale(v, nowMs)) return 'STALE'
  const known = v.link !== undefined && isValid(v.link.header, LinkStatusValid.state)
  return linkLabel(known ? v.link?.state : undefined)
}
