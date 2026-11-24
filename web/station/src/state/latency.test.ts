import { expect, test } from 'vitest'
import { LatencyWindow, countsForLatency } from './latency'
import { health, linkMsg, poseMsg, vehicleState } from '../test/fixtures'
import type { SampleMsg } from '../net/protocol'

test('LatencyWindow nearest-rank and expiry', () => {
  const w = new LatencyWindow(1000); [5,1,4,2,3].forEach((v,i) => w.add(v, i))
  expect(w.stats(10)).toEqual({ p50: 3, p99: 5, count: 5 }); expect(w.stats(2000)).toBeNull()
})

test('nearest rank picks sorted[ceil(q*n) - 1], never interpolates', () => {
  const w = new LatencyWindow()
  for (let v = 100; v >= 1; v--) w.add(v, 0)
  expect(w.stats(0)).toEqual({ p50: 50, p99: 99, count: 100 })
  const one = new LatencyWindow()
  one.add(42.5, 0)
  expect(one.stats(0)).toEqual({ p50: 42.5, p99: 42.5, count: 1 })
})

test('only samples inside the window count', () => {
  const w = new LatencyWindow(1000)
  w.add(900, 0)
  w.add(10, 600)
  w.add(20, 1200)
  expect(w.stats(1200)).toEqual({ p50: 10, p99: 20, count: 2 })
  expect(w.stats(1700)).toEqual({ p50: 20, p99: 20, count: 1 })
})

test('negative ages are kept, as the benchmark keeps them', () => {
  const w = new LatencyWindow()
  w.add(-2, 0)
  w.add(3, 0)
  expect(w.stats(0)).toEqual({ p50: -2, p99: 3, count: 2 })
})

test('only pose ages count, as in the benchmark; other topics and unknown ages do not', () => {
  expect(countsForLatency(poseMsg(1, 47.4, 8.5, 3))).toBe(true)
  expect(countsForLatency({ type: 'sample', topic: 'vehicle_state', data: vehicleState, age_ms: 180 })).toBe(false)
  expect(countsForLatency({ type: 'sample', topic: 'health', data: health, age_ms: 400 })).toBe(false)
  expect(countsForLatency(poseMsg(1, 47.4, 8.5, null))).toBe(false)
  expect(countsForLatency(linkMsg(1, 'LINK_CONNECTED', 950))).toBe(false)
  const infoMsg = { type: 'sample', topic: 'vehicle_info', data: {}, age_ms: 28_000 } as unknown as SampleMsg
  expect(countsForLatency(infoMsg)).toBe(false)
})
