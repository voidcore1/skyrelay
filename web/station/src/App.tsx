// The station: one gateway connection feeding the fleet state, the map, the
// fleet panel and the latency HUD.
import { useEffect, useState, type CSSProperties } from 'react'
import { FleetPanel } from './components/FleetPanel'
import { InsecureBanner } from './components/InsecureBanner'
import { LatencyHud } from './components/LatencyHud'
import { MapView } from './map/MapView'
import { GatewayClient, type GatewayStatus } from './net/gatewayClient'
import type { HelloMsg } from './net/protocol'
import { applySample, type FleetState } from './state/fleetStore'
import { LatencyWindow, countsForLatency, type LatencyStats } from './state/latency'
import { LINK_COLOR, type LinkLabel } from './state/link'

const HUD_REFRESH_MS = 500

// Same origin as the page: the gateway serves the station in production, and
// Vite's proxy forwards /ws in development.
const GATEWAY_URL = `${window.location.protocol === 'https:' ? 'wss' : 'ws'}://${window.location.host}/ws`

const STATUS_TEXT: Record<GatewayStatus, string> = {
  connecting: 'CONNECTING',
  open: 'OPEN',
  closed: 'DOWN · RETRYING',
}

// The gateway connection is a link too, so it borrows the link colours.
const STATUS_LINK: Record<GatewayStatus, LinkLabel> = {
  connecting: 'RECOVERING',
  open: 'CONNECTED',
  closed: 'LOST',
}

function utcClock(ms: number): string {
  return new Date(ms).toISOString().slice(11, 19)
}

export default function App() {
  const [hello, setHello] = useState<HelloMsg>()
  const [status, setStatus] = useState<GatewayStatus>('connecting')
  const [fleet, setFleet] = useState<FleetState>({})
  const [latency, setLatency] = useState<LatencyStats | null>(null)
  const [rtt, setRtt] = useState<number>()
  const [now, setNow] = useState(() => Date.now())

  useEffect(() => {
    const client = new GatewayClient(GATEWAY_URL)
    const window10s = new LatencyWindow()
    // Samples arrive in bursts; fold them into a plain variable and render at
    // most once per animation frame.
    let current: FleetState = {}
    let frame = 0
    const render = () => {
      frame = 0
      setFleet(current)
    }
    client.onHello(setHello)
    client.onStatus(setStatus)
    client.onSample((s) => {
      if (countsForLatency(s)) window10s.add(s.age_ms, performance.now())
      current = applySample(current, s)
      if (frame === 0) frame = requestAnimationFrame(render)
    })
    client.connect()
    const tick = setInterval(() => {
      setLatency(window10s.stats(performance.now()))
      setRtt(client.rttMs())
      setNow(Date.now())
    }, HUD_REFRESH_MS)
    return () => {
      client.close()
      clearInterval(tick)
      cancelAnimationFrame(frame)
    }
  }, [])

  const hasData = Object.keys(fleet).length > 0
  const stale = hasData && status !== 'open'

  return (
    <div className="station">
      <InsecureBanner hello={hello} />
      <header className="topbar">
        <h1 className="wordmark">
          SKYRELAY<span className="wordmark-sub">/ STATION</span>
        </h1>
        <dl className="meta">
          <div>
            <dt>GATEWAY</dt>
            <dd className="gw" style={{ '--state': LINK_COLOR[STATUS_LINK[status]] } as CSSProperties}>
              {STATUS_TEXT[status]}
            </dd>
          </div>
          <div>
            <dt>ENDPOINT</dt>
            <dd>{GATEWAY_URL}</dd>
          </div>
          <div>
            <dt>VERSION</dt>
            <dd>{hello?.version ?? '—'}</dd>
          </div>
          <div>
            <dt>CONTRACT</dt>
            <dd>{hello?.contract ?? '—'}</dd>
          </div>
        </dl>
        <time className="clock" dateTime={new Date(now).toISOString()}>
          {utcClock(now)}
          <span className="clock-zone">UTC</span>
        </time>
      </header>
      <main className="workspace">
        <MapView fleet={fleet} stale={stale} />
        <FleetPanel fleet={fleet} stale={stale} />
      </main>
      <footer className="statusbar">
        <LatencyHud stats={latency} rttMs={rtt} />
        <span className="statusbar-note">pose age = gateway send − bridge decode · nearest-rank · 10 s window</span>
      </footer>
    </div>
  )
}
