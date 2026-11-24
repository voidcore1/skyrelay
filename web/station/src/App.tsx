// The station shares its fleet panel and latency HUD across telemetry sources.
import { useEffect, useState, type CSSProperties, type ReactNode } from 'react'
import { FleetPanel } from './components/FleetPanel'
import { InsecureBanner } from './components/InsecureBanner'
import { LatencyHud } from './components/LatencyHud'
import { isDemo } from './demo/mode'
import { ReplayControls } from './demo/ReplayControls'
import { SchematicMap } from './demo/SchematicMap'
import { demoReplayFrames, REPLAY_SECONDS, REPLAY_VEHICLE_ID } from './demo/snapshot'
import { MapView } from './map/MapView'
import { GatewayClient, type GatewayStatus } from './net/gatewayClient'
import type { HelloMsg } from './net/protocol'
import { applySample, type FleetState } from './state/fleetStore'
import { LatencyWindow, countsForLatency, type LatencyStats } from './state/latency'
import { LINK_COLOR, linkLabel, type LinkLabel } from './state/link'

const HUD_REFRESH_MS = 500
const REPLAY_DURATION_MS = REPLAY_SECONDS * 1000

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
  return isDemo(window.location.search) ? <DemoStation /> : <LiveStation />
}

function DemoStation() {
  const [frames] = useState(demoReplayFrames)
  const [elapsedMs, setElapsedMs] = useState(0)
  const [startedAt, setStartedAt] = useState<number | null>(null)

  useEffect(() => {
    if (startedAt === null) return
    const timer = setInterval(() => {
      const elapsed = Math.min(performance.now() - startedAt, REPLAY_DURATION_MS)
      setElapsedMs(elapsed)
      if (elapsed === REPLAY_DURATION_MS) setStartedAt(null)
    }, 100)
    return () => clearInterval(timer)
  }, [startedAt])

  const ended = elapsedMs === REPLAY_DURATION_MS
  const { fleet, latency } = frames[Math.floor(elapsedMs / 1000)]!

  function togglePlayback() {
    if (startedAt !== null) {
      setElapsedMs(Math.min(performance.now() - startedAt, REPLAY_DURATION_MS))
      setStartedAt(null)
    } else {
      if (ended) setElapsedMs(0)
      setStartedAt(performance.now() - (ended ? 0 : elapsedMs))
    }
  }

  function resetPlayback() {
    setStartedAt(null)
    setElapsedMs(0)
  }

  return (
    <Station fleet={fleet} latency={latency} demoControls={
      <ReplayControls
        elapsedMs={elapsedMs}
        playing={startedAt !== null}
        ended={ended}
        link={linkLabel(fleet[REPLAY_VEHICLE_ID]?.link?.state)}
        onToggle={togglePlayback}
        onReset={resetPlayback}
      />
    } />
  )
}

function LiveStation() {
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

  return <Station fleet={fleet} latency={latency} live={{ hello, status, rtt, now }} />
}

interface StationProps {
  fleet: FleetState
  latency: LatencyStats | null
  demoControls?: ReactNode
  live?: {
    hello: HelloMsg | undefined
    status: GatewayStatus
    rtt: number | undefined
    now: number
  }
}

function Station({ fleet, latency, demoControls, live }: StationProps) {
  const stale = live !== undefined && live.status !== 'open' && Object.keys(fleet).length > 0

  return (
    <div className={live ? 'station' : 'station station-demo'}>
      {live && <InsecureBanner hello={live.hello} />}
      <header className="topbar">
        <h1 className="wordmark">
          SKYRELAY<span className="wordmark-sub">/ STATION</span>
        </h1>
        {live ? (
          <>
            <dl className="meta">
              <div>
                <dt>GATEWAY</dt>
                <dd className="gw" style={{ '--state': LINK_COLOR[STATUS_LINK[live.status]] } as CSSProperties}>
                  {STATUS_TEXT[live.status]}
                </dd>
              </div>
              <div>
                <dt>ENDPOINT</dt>
                <dd>{GATEWAY_URL}</dd>
              </div>
              <div>
                <dt>VERSION</dt>
                <dd>{live.hello?.version ?? '—'}</dd>
              </div>
              <div>
                <dt>CONTRACT</dt>
                <dd>{live.hello?.contract ?? '—'}</dd>
              </div>
            </dl>
            <time className="clock" dateTime={new Date(live.now).toISOString()}>
              {utcClock(live.now)}
              <span className="clock-zone">UTC</span>
            </time>
          </>
        ) : (
          <>
            <p className="demo-label">Synthetic demo</p>
            {demoControls}
          </>
        )}
      </header>
      <main className="workspace">
        {live ? <MapView fleet={fleet} stale={stale} /> : <SchematicMap fleet={fleet} />}
        <FleetPanel fleet={fleet} stale={stale} synthetic={!live} />
      </main>
      <footer className="statusbar">
        <LatencyHud stats={latency} rttMs={live?.rtt} synthetic={!live} />
        <span className="statusbar-note">
          {live ? 'pose age = gateway send − bridge decode · nearest-rank · 10 s window' : 'synthetic samples · nearest-rank · 10 s window'}
        </span>
      </footer>
    </div>
  )
}
