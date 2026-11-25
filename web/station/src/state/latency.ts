// Telemetry age over a sliding window, for the HUD. The percentile is
// nearest-rank, sorted[ceil(q*n) - 1], the same definition as
// bench/report/latency.py, so the HUD and the benchmark report agree on what
// p99 means. The HUD numbers are still only indicative: they include the time
// a sample waited for this browser, and the benchmark does not.
import type { SampleMsg } from '../net/protocol'

export interface LatencyStats {
  p50: number
  p99: number
  count: number
}

export function nearestRank(sorted: readonly number[], q: number): number {
  const rank = Math.max(Math.ceil(q * sorted.length), 1)
  return sorted[rank - 1]!
}

export class LatencyWindow {
  private readonly windowMs: number
  // [time added, age], oldest first.
  private entries: Array<[number, number]> = []

  constructor(windowMs = 10_000) {
    this.windowMs = windowMs
  }

  add(ageMs: number, nowMs: number): void {
    this.entries.push([nowMs, ageMs])
  }

  stats(nowMs: number): LatencyStats | null {
    this.expire(nowMs)
    if (this.entries.length === 0) return null
    const sorted = this.entries.map(([, age]) => age).sort((a, b) => a - b)
    return { p50: nearestRank(sorted, 0.5), p99: nearestRank(sorted, 0.99), count: sorted.length }
  }

  private expire(nowMs: number): void {
    const cutoff = nowMs - this.windowMs
    let keepFrom = 0
    while (keepFrom < this.entries.length && this.entries[keepFrom]![0] <= cutoff) keepFrom++
    if (keepFrom > 0) this.entries = this.entries.slice(keepFrom)
  }
}

// Which samples measure delivery latency: pose only, the topic the benchmark
// measures. Pose is published as each position message is decoded, so its age
// is bridge + DDS + gateway. vehicle_state and health are republished at a
// fixed rate from the last decoded message, so their age also holds how old
// that message was (hundreds of milliseconds with the simulator). vehicle_info
// and link_status are latched, and the replay every new client gets carries the
// age of the stored sample, half a minute for vehicle_info.
export function countsForLatency(msg: SampleMsg): msg is SampleMsg & { topic: 'pose'; age_ms: number } {
  return msg.topic === 'pose' && msg.age_ms !== null
}
