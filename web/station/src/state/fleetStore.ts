// What the station knows about each vehicle: the latest sample of every topic.
// applySample is a pure reducer, so React can tell what changed by identity:
// a sample replaces only the vehicle it is about.
import type { Health, LinkStatus, Pose, VehicleInfo, VehicleState } from '../contract/v1'
import type { SampleMsg } from '../net/protocol'

export interface VehicleView {
  id: number
  info?: VehicleInfo
  state?: VehicleState
  pose?: Pose
  health?: Health
  link?: LinkStatus
  // age_ms of the most recent sample that had one.
  lastAgeMs?: number
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
