// Messages of the gateway WebSocket, as described in docs/gateway-protocol.md.
// The payload types come from the generated contract.
import type { Health, LinkStatus, Pose, VehicleInfo, VehicleState } from '../contract/v1'

export interface HelloMsg {
  type: 'hello'
  version: string
  contract: string
  insecure: boolean
  server_time_ns: number
}

export interface PongMsg {
  type: 'pong'
  id: number | null
  t_client_ms: number | null
  server_time_ns: number
}

export interface PingMsg {
  type: 'ping'
  id: number
  t_client_ms: number
}

interface Sample<Topic extends string, Data> {
  type: 'sample'
  topic: Topic
  data: Data
  // Gateway send time minus bridge decode time; null when the bridge time is
  // unknown. Can be negative when the two clocks disagree.
  age_ms: number | null
}

export type SampleMsg =
  | Sample<'vehicle_info', VehicleInfo>
  | Sample<'vehicle_state', VehicleState>
  | Sample<'pose', Pose>
  | Sample<'health', Health>
  | Sample<'link_status', LinkStatus>

export type Topic = SampleMsg['topic']

// The vehicle's data on this topic no longer has a live writer in DDS (the
// bridge exited, crashed, or disposed it). A later sample means it is back.
export interface GoneMsg {
  type: 'gone'
  topic: Topic
  vehicle_id: number
}
