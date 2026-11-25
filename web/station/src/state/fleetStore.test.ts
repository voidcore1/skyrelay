import { expect, test } from 'vitest'
import { applyGone, applySample, receiveSample, STALE_AFTER_MS, vehicleLink, type FleetState } from './fleetStore'
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

test('a vehicle turns STALE once its link_status is more than 3 s old', () => {
  const s = receiveSample({}, linkMsg(2, 'LINK_CONNECTED', 1), 10_000)
  const v = s[2]!
  expect(vehicleLink(v, 10_000)).toBe('CONNECTED')
  expect(vehicleLink(v, 10_000 + STALE_AFTER_MS - 1)).toBe('CONNECTED')
  expect(vehicleLink(v, 10_000 + STALE_AFTER_MS + 1)).toBe('STALE')
  // Without a clock (the demo) nothing goes stale by time.
  expect(vehicleLink(v)).toBe('CONNECTED')

  // A fresh link_status brings it back.
  const s2 = receiveSample(s, linkMsg(2, 'LINK_DEGRADED', 1), 14_000)
  expect(vehicleLink(s2[2]!, 14_500)).toBe('DEGRADED')
})

test('a replayed link_status counts from when the bridge sent it', () => {
  // The latched sample a new client gets is already 2.5 s old.
  const s = receiveSample({}, linkMsg(4, 'LINK_CONNECTED', 2500), 50_000)
  expect(vehicleLink(s[4]!, 50_400)).toBe('CONNECTED')
  expect(vehicleLink(s[4]!, 50_600)).toBe('STALE')
  // A negative age (clocks that disagree) does not push it into the future.
  const n = receiveSample({}, linkMsg(4, 'LINK_CONNECTED', -800), 50_000)
  expect(n[4]!.linkCurrentAt).toBe(50_000)
})

test('other topics do not keep a vehicle fresh', () => {
  let s = receiveSample({}, linkMsg(1, 'LINK_CONNECTED', 1), 0)
  s = receiveSample(s, poseMsg(1, 47.39, 8.54), 3_500)
  expect(vehicleLink(s[1]!, 3_600)).toBe('STALE')
})

test('a gone notice for link_status marks the vehicle STALE at once and keeps its data', () => {
  const s = receiveSample(applySample({}, poseMsg(3, 47.39, 8.54)), linkMsg(3, 'LINK_CONNECTED', 1), 1_000)
  const g = applyGone(s, { type: 'gone', topic: 'link_status', vehicle_id: 3 })
  expect(vehicleLink(g[3]!, 1_001)).toBe('STALE')
  expect(vehicleLink(g[3]!)).toBe('STALE')
  expect(g[3]!.pose?.lat_deg).toBe(47.39)
  // Gone for another topic, or for a vehicle the station never saw, changes nothing.
  expect(applyGone(s, { type: 'gone', topic: 'pose', vehicle_id: 3 })).toBe(s)
  expect(applyGone(s, { type: 'gone', topic: 'link_status', vehicle_id: 9 })).toBe(s)
  // The next link_status clears it.
  const back = receiveSample(g, linkMsg(3, 'LINK_RECOVERING', 1), 2_000)
  expect(vehicleLink(back[3]!, 2_001)).toBe('RECOVERING')
})

test('a link_status whose state bit is cleared is no data, not LOST', () => {
  const msg = linkMsg(5, 'LINK_LOST')
  if (msg.topic !== 'link_status') throw new Error('fixture')
  const cleared = { ...msg, data: { ...msg.data, header: { ...msg.data.header, valid_mask: 0 } } }
  expect(vehicleLink(applySample({}, cleared)[5]!)).toBe('UNKNOWN')
})
