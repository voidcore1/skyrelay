// Link state as the station shows it. One colour per state, used by the fleet
// panel and the map alike, and never for anything else. UNKNOWN means no
// link_status has arrived yet; STALE means the last one is too old to trust
// (see fleetStore), whatever state it reported.
import type { LinkState } from '../contract/v1'

export type LinkLabel = 'CONNECTED' | 'DEGRADED' | 'LOST' | 'RECOVERING' | 'STALE' | 'UNKNOWN'

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
  STALE: '#8a8a85',
  UNKNOWN: '#8a8a85',
}

// Draw order on the map, worst on top, so a lost vehicle is never hidden
// under a healthy one at the same spot.
export const LINK_SEVERITY: Readonly<Record<LinkLabel, number>> = {
  CONNECTED: 0,
  UNKNOWN: 1,
  STALE: 2,
  RECOVERING: 3,
  DEGRADED: 4,
  LOST: 5,
}

// What the panel and the map print for a label.
export function linkText(l: LinkLabel): string {
  return l === 'UNKNOWN' ? 'NO DATA' : l
}
