import { expect, test } from 'vitest'
import { applySample, type FleetState } from './fleetStore'
import { health, info, linkMsg, poseMsg, vehicleState } from '../test/fixtures'
import type { SampleMsg } from '../net/protocol'

test('applySample updates only the touched vehicle', () => {
  const s0 = applySample({}, poseMsg(1, 47.39, 8.54)); const s1 = applySample(s0, poseMsg(2, 47.4, 8.55))
  expect(s1[1]).toBe(s0[1]); expect(s1[2].pose?.lat_deg).toBe(47.4)
})

test('applySample does not mutate the previous state', () => {
  const s0 = applySample({}, poseMsg(1, 47.39, 8.54))
  const s1 = applySample(s0, poseMsg(1, 47.5, 8.6))
  expect(s0[1].pose?.lat_deg).toBe(47.39)
  expect(s1[1].pose?.lat_deg).toBe(47.5)
  expect(s1).not.toBe(s0)
})

test('each topic lands in its own slot and the vehicle keeps the others', () => {
  const msgs: SampleMsg[] = [
    { type: 'sample', topic: 'vehicle_info', data: { ...info, vehicle_id: 3 }, age_ms: 28_000 },
    { type: 'sample', topic: 'vehicle_state', data: { ...vehicleState, vehicle_id: 3, armed: true }, age_ms: 2 },
    { type: 'sample', topic: 'health', data: { ...health, vehicle_id: 3, battery_remaining_pct: 81 }, age_ms: 3 },
    linkMsg(3, 'LINK_DEGRADED', 4),
    poseMsg(3, 47.4, 8.55, 5),
  ]
  const s: FleetState = msgs.reduce(applySample, {})
  const v = s[3]
  expect(v.id).toBe(3)
  expect(v.info?.autopilot).toBe('AUTOPILOT_PX4')
  expect(v.state?.armed).toBe(true)
  expect(v.health?.battery_remaining_pct).toBe(81)
  expect(v.link?.state).toBe('LINK_DEGRADED')
  expect(v.pose?.lon_deg).toBe(8.55)
  expect(v.lastAgeMs).toBe(5)
})

test('a null age keeps the last known age', () => {
  const s0 = applySample({}, poseMsg(1, 47.39, 8.54, 7))
  const s1 = applySample(s0, poseMsg(1, 47.39, 8.54, null))
  expect(s1[1].lastAgeMs).toBe(7)
})

test('a sample of a topic this station does not know is ignored', () => {
  const s0 = applySample({}, poseMsg(1, 47.39, 8.54))
  const unknown = { type: 'sample', topic: 'mission', data: { vehicle_id: 1 }, age_ms: 1 } as unknown as SampleMsg
  expect(applySample(s0, unknown)).toBe(s0)
})
