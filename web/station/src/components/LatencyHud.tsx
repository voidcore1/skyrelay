// Telemetry age (nearest-rank p50/p99 over the last 10 s) and the round trip
// to the gateway. Indicative only: browser timing is not the benchmark.
import type { LatencyStats } from '../state/latency'
import { fmtMs } from './format'

const TARGET_P99_MS = 100

export function LatencyHud({ stats, rttMs }: { stats: LatencyStats | null; rttMs: number | undefined }) {
  const over = stats !== null && stats.p99 > TARGET_P99_MS
  return (
    <div className="hud" aria-label="Telemetry latency">
      <span className="hud-k hud-title">telem</span>{' '}
      <span className="hud-k">p50</span> <data className="hud-v" value={stats?.p50}>{fmtMs(stats?.p50)}</data>
      <span className="hud-u">ms</span>{' '}
      <span className="hud-k">p99</span>{' '}
      <data className="hud-v" data-over={over || undefined} value={stats?.p99}>
        {fmtMs(stats?.p99)}
      </data>
      <span className="hud-u">ms</span> <span className="hud-sep">·</span> <span className="hud-k">rtt</span>{' '}
      <data className="hud-v hud-v-small" value={rttMs}>{fmtMs(rttMs)}</data>
      <span className="hud-u">ms</span> <span className="hud-sep">·</span> <span className="hud-k hud-note">indicative</span>
      <span className="hud-n" title="samples in the window">
        {' '}
        n={stats?.count ?? 0}/10s
      </span>
    </div>
  )
}
