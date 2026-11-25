import { afterEach, beforeEach, expect, test, vi } from 'vitest'
import { GatewayClient } from './gatewayClient'
import type { GoneMsg, HelloMsg, SampleMsg } from './protocol'
import { linkMsg, poseMsg } from '../test/fixtures'

// Just enough of a browser WebSocket for the client: on* handlers, send, close.
class FakeSocket {
  static readonly CONNECTING = 0
  static readonly OPEN = 1
  static readonly CLOSING = 2
  static readonly CLOSED = 3
  static instances: FakeSocket[] = []

  readyState = FakeSocket.CONNECTING
  sent: string[] = []
  onopen: ((ev: Event) => void) | null = null
  onmessage: ((ev: MessageEvent) => void) | null = null
  onclose: ((ev: CloseEvent) => void) | null = null
  onerror: ((ev: Event) => void) | null = null
  readonly url: string

  constructor(url: string) {
    this.url = url
    FakeSocket.instances.push(this)
  }
  send(data: string) {
    this.sent.push(data)
  }
  close() {
    this.drop()
  }
  // Test side.
  accept() {
    this.readyState = FakeSocket.OPEN
    this.onopen?.(new Event('open'))
  }
  deliver(msg: unknown) {
    this.onmessage?.(new MessageEvent('message', { data: typeof msg === 'string' ? msg : JSON.stringify(msg) }))
  }
  drop() {
    if (this.readyState === FakeSocket.CLOSED) return
    this.readyState = FakeSocket.CLOSED
    this.onclose?.(new CloseEvent('close'))
  }
}

const latest = () => FakeSocket.instances.at(-1)!
const hello: HelloMsg = { type: 'hello', version: '0.1.0-preview', contract: '1.0.0-preview.1', insecure: true, server_time_ns: 1 }

beforeEach(() => {
  FakeSocket.instances = []
  vi.stubGlobal('WebSocket', FakeSocket)
  vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout', 'setInterval', 'clearInterval', 'Date', 'performance'] })
})
afterEach(() => {
  vi.useRealTimers()
  vi.unstubAllGlobals()
})

// Drops the current socket and returns how long the client waited before the
// next attempt.
function delayAfterDrop(): number {
  const before = FakeSocket.instances.length
  latest().drop()
  let waited = 0
  while (FakeSocket.instances.length === before) {
    if (waited > 60_000) throw new Error('client never reconnected')
    vi.advanceTimersByTime(10)
    waited += 10
  }
  return waited
}

test('client reconnects with doubling backoff capped at 5 s', () => {
  const c = new GatewayClient('ws://127.0.0.1:8080/ws')
  c.connect()
  expect(FakeSocket.instances).toHaveLength(1)
  expect(latest().url).toBe('ws://127.0.0.1:8080/ws')
  const delays = Array.from({ length: 6 }, delayAfterDrop)
  expect(delays).toEqual([500, 1000, 2000, 4000, 5000, 5000])
  c.close()
})

test('the backoff starts again at 500 ms after a hello', () => {
  const c = new GatewayClient('ws://127.0.0.1:8080/ws')
  c.connect()
  expect([delayAfterDrop(), delayAfterDrop(), delayAfterDrop()]).toEqual([500, 1000, 2000])
  latest().accept()
  expect(delayAfterDrop()).toBe(4000) // an open socket without hello does not count
  latest().accept()
  latest().deliver(hello)
  expect(delayAfterDrop()).toBe(500)
  c.close()
})

test('the backoff range can be configured', () => {
  const c = new GatewayClient('ws://x/ws', { backoffMs: [100, 300] })
  c.connect()
  expect([delayAfterDrop(), delayAfterDrop(), delayAfterDrop()]).toEqual([100, 200, 300])
  c.close()
})

test('close stops the client for good', () => {
  const c = new GatewayClient('ws://x/ws')
  const statuses: string[] = []
  c.onStatus((s) => statuses.push(s))
  c.connect()
  latest().accept()
  c.close()
  vi.advanceTimersByTime(60_000)
  expect(FakeSocket.instances).toHaveLength(1)
  expect(statuses).toEqual(['connecting', 'open', 'closed'])
})

test('reports status, hello and every sample of a batch in order', () => {
  const c = new GatewayClient('ws://x/ws')
  const statuses: string[] = []
  const hellos: HelloMsg[] = []
  const samples: SampleMsg[] = []
  c.onStatus((s) => statuses.push(s))
  c.onHello((h) => hellos.push(h))
  c.onSample((s) => samples.push(s))
  c.connect()
  latest().accept()
  latest().deliver(hello)
  const batch = [poseMsg(1, 47.39, 8.54), linkMsg(2, 'LINK_LOST')]
  latest().deliver(batch)
  latest().deliver('{not json')
  latest().deliver([{ type: 'pong', id: 9, t_client_ms: 1, server_time_ns: 1 }, poseMsg(3, 47.4, 8.55)])
  latest().deliver({ type: 'pong', id: 99, t_client_ms: 1, server_time_ns: 1 })
  latest().drop()
  expect(statuses).toEqual(['connecting', 'open', 'closed'])
  expect(hellos).toEqual([hello])
  expect(samples).toEqual([...batch, poseMsg(3, 47.4, 8.55)])
  vi.advanceTimersByTime(500)
  expect(statuses.at(-1)).toBe('connecting')
  c.close()
})

test('pings every 2 s and keeps an EWMA of the round trip', () => {
  const c = new GatewayClient('ws://x/ws')
  c.connect()
  const sock = latest()
  sock.accept()
  expect(c.rttMs()).toBeUndefined()

  const pongFor = (raw: string | undefined) => {
    const ping = JSON.parse(raw!) as { type: string; id: number; t_client_ms: number }
    expect(ping.type).toBe('ping')
    return { type: 'pong', id: ping.id, t_client_ms: ping.t_client_ms, server_time_ns: 1 }
  }

  expect(sock.sent).toHaveLength(1) // the first ping goes out on open
  vi.advanceTimersByTime(10)
  sock.deliver(pongFor(sock.sent[0]))
  expect(c.rttMs()).toBeCloseTo(10)

  vi.advanceTimersByTime(1990)
  expect(sock.sent).toHaveLength(2)
  vi.advanceTimersByTime(20)
  sock.deliver(pongFor(sock.sent[1]))
  expect(c.rttMs()).toBeCloseTo(0.8 * 10 + 0.2 * 20)

  // A pong for a ping that was never sent changes nothing.
  sock.deliver({ type: 'pong', id: 12345, t_client_ms: 0, server_time_ns: 1 })
  expect(c.rttMs()).toBeCloseTo(12)

  // No pings while closed, and the estimate goes with the connection.
  sock.drop()
  expect(c.rttMs()).toBeUndefined()
  vi.advanceTimersByTime(400)
  expect(sock.sent).toHaveLength(2)
  c.close()
})

test('gone notices in a batch reach onGone, between the samples around them', () => {
  const c = new GatewayClient('ws://127.0.0.1:8080/ws')
  const seen: string[] = []
  const gone: GoneMsg[] = []
  c.onSample((s) => seen.push(`sample ${s.topic} ${s.data.vehicle_id}`))
  c.onGone((g) => {
    gone.push(g)
    seen.push(`gone ${g.topic} ${g.vehicle_id}`)
  })
  c.connect()
  latest().accept()
  latest().deliver(hello)
  latest().deliver([
    linkMsg(1, 'LINK_CONNECTED'),
    { type: 'gone', topic: 'link_status', vehicle_id: 2 },
    { type: 'gone', topic: 'link_status' },
    { type: 'gone', vehicle_id: 3 },
    poseMsg(1, 47.4, 8.55),
  ])
  expect(seen).toEqual(['sample link_status 1', 'gone link_status 2', 'sample pose 1'])
  expect(gone).toEqual([{ type: 'gone', topic: 'link_status', vehicle_id: 2 }])
  c.close()
})
