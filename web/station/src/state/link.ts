// Link state as the station shows it. One colour per state, used by the fleet
// panel and the map alike, and never for anything else.
import type { LinkState } from '../contract/v1'

export type LinkLabel = 'CONNECTED' | 'DEGRADED' | 'LOST' | 'RECOVERING' | 'UNKNOWN'

const labels: Record<LinkState, LinkLabel> = {
  LINK_CONNECTED: 'CONNECTED',
  LINK_DEGRADED: 'DEGRADED',
  LINK_LOST: 'LOST',
  LINK_RECOVERING: 'RECOVERING',
}

export function linkLabel(state: LinkState | undefined): LinkLabel {
  return (state !== undefined && labels[state]) || 'UNKNOWN'
}

export const LINK_COLOR: Readonly<Record<LinkLabel, string>> = {
  CONNECTED: '#4af626',
  DEGRADED: '#ffb000',
  LOST: '#ff2a2a',
  RECOVERING: '#2fc6ff',
  UNKNOWN: '#8a8a85',
}

// Draw order on the map, worst on top, so a lost vehicle is never hidden
// under a healthy one at the same spot.
export const LINK_SEVERITY: Readonly<Record<LinkLabel, number>> = {
  CONNECTED: 0,
  UNKNOWN: 1,
  RECOVERING: 2,
  DEGRADED: 3,
  LOST: 4,
}
