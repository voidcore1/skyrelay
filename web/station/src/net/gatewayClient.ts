// WebSocket client for the gateway (docs/gateway-protocol.md).
//
// It reconnects on its own: after a drop it waits 500 ms, then doubles the wait
// up to 5 s, and starts again at 500 ms once a new connection has said hello.
// While open it pings every 2 s and keeps an EWMA (alpha 0.2) of the round
// trip, which the HUD shows next to the telemetry age.
import type { HelloMsg, PingMsg, PongMsg, SampleMsg } from './protocol'

export type GatewayStatus = 'connecting' | 'open' | 'closed'

const PING_INTERVAL_MS = 2000
const RTT_ALPHA = 0.2
// Pings still waiting for their pong; older ones are forgotten.
const MAX_OUTSTANDING_PINGS = 8

const isObject = (v: unknown): v is Record<string, unknown> => typeof v === 'object' && v !== null

export class GatewayClient {
  private readonly url: string
  private readonly minBackoffMs: number
  private readonly maxBackoffMs: number
  private backoffMs: number

  private ws: WebSocket | null = null
  private stopped = true
  private reconnectTimer: ReturnType<typeof setTimeout> | undefined
  private pingTimer: ReturnType<typeof setInterval> | undefined
  private nextPingId = 1
  private readonly outstanding = new Map<number, number>() // ping id -> sent at
  private rtt: number | undefined

  private readonly helloCbs: Array<(h: HelloMsg) => void> = []
  private readonly sampleCbs: Array<(s: SampleMsg) => void> = []
  private readonly statusCbs: Array<(s: GatewayStatus) => void> = []

  constructor(url: string, opts?: { backoffMs?: [number, number] }) {
    this.url = url
    const [min, max] = opts?.backoffMs ?? [500, 5000]
    this.minBackoffMs = min
    this.maxBackoffMs = max
    this.backoffMs = min
  }

  onHello(cb: (h: HelloMsg) => void): void {
    this.helloCbs.push(cb)
  }

  onSample(cb: (s: SampleMsg) => void): void {
    this.sampleCbs.push(cb)
  }

  onStatus(cb: (s: GatewayStatus) => void): void {
    this.statusCbs.push(cb)
  }

  connect(): void {
    if (!this.stopped) return
    this.stopped = false
    this.backoffMs = this.minBackoffMs
    this.open()
  }

  close(): void {
    if (this.stopped) return
    this.stopped = true
    clearTimeout(this.reconnectTimer)
    this.reconnectTimer = undefined
    const ws = this.ws
    if (ws) {
      this.detach(ws)
      ws.close()
      this.ws = null
      this.connectionLost()
    }
  }

  rttMs(): number | undefined {
    return this.rtt
  }

  private open(): void {
    this.reconnectTimer = undefined
    this.emitStatus('connecting')
    const ws = new WebSocket(this.url)
    this.ws = ws
    ws.onopen = () => {
      this.emitStatus('open')
      this.ping()
      this.pingTimer = setInterval(() => this.ping(), PING_INTERVAL_MS)
    }
    ws.onmessage = (ev: MessageEvent) => {
      if (typeof ev.data === 'string') this.handle(ev.data)
    }
    // A failed connection attempt also ends in close, so error needs nothing.
    ws.onerror = () => {}
    ws.onclose = () => {
      this.detach(ws)
      this.ws = null
      this.connectionLost()
      if (!this.stopped) this.scheduleReconnect()
    }
  }

  private detach(ws: WebSocket): void {
    ws.onopen = ws.onmessage = ws.onerror = ws.onclose = null
  }

  private connectionLost(): void {
    clearInterval(this.pingTimer)
    this.pingTimer = undefined
    this.outstanding.clear()
    this.rtt = undefined
    this.emitStatus('closed')
  }

  private scheduleReconnect(): void {
    const delay = this.backoffMs
    this.backoffMs = Math.min(this.backoffMs * 2, this.maxBackoffMs)
    this.reconnectTimer = setTimeout(() => this.open(), delay)
  }

  private ping(): void {
    const ws = this.ws
    if (!ws || ws.readyState !== WebSocket.OPEN) return
    const msg: PingMsg = { type: 'ping', id: this.nextPingId++, t_client_ms: performance.now() }
    this.outstanding.set(msg.id, msg.t_client_ms)
    if (this.outstanding.size > MAX_OUTSTANDING_PINGS) {
      const oldest = this.outstanding.keys().next().value
      if (oldest !== undefined) this.outstanding.delete(oldest)
    }
    ws.send(JSON.stringify(msg))
  }

  private handle(text: string): void {
    let msg: unknown
    try {
      msg = JSON.parse(text)
    } catch {
      return // not JSON; nothing the protocol sends, so ignore it
    }
    if (Array.isArray(msg)) {
      for (const s of msg) {
        if (isObject(s) && s.type === 'sample' && isObject(s.data) && typeof s.data.vehicle_id === 'number') {
          for (const cb of this.sampleCbs) cb(s as unknown as SampleMsg)
        }
      }
      return
    }
    if (!isObject(msg)) return
    if (msg.type === 'hello') {
      this.backoffMs = this.minBackoffMs
      for (const cb of this.helloCbs) cb(msg as unknown as HelloMsg)
    } else if (msg.type === 'pong') {
      this.handlePong(msg as unknown as PongMsg)
    }
  }

  private handlePong(pong: PongMsg): void {
    if (typeof pong.id !== 'number') return
    const sentAt = this.outstanding.get(pong.id)
    if (sentAt === undefined) return
    this.outstanding.delete(pong.id)
    const sample = performance.now() - sentAt
    this.rtt = this.rtt === undefined ? sample : (1 - RTT_ALPHA) * this.rtt + RTT_ALPHA * sample
  }

  private emitStatus(s: GatewayStatus): void {
    for (const cb of this.statusCbs) cb(s)
  }
}
