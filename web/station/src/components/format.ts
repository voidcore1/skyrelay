// Display formatting shared by the panels. Anything unknown is a dash.
import type { AutopilotKind, FlightMode } from '../contract/v1'

export const DASH = '—'

// One decimal below 10 ms, whole milliseconds above.
export function fmtMs(v: number | null | undefined): string {
  if (v === null || v === undefined || !Number.isFinite(v)) return DASH
  return Math.abs(v) < 10 ? v.toFixed(1) : Math.round(v).toString()
}

export function fmtAutopilot(a: AutopilotKind | null | undefined): string {
  switch (a) {
    case 'AUTOPILOT_PX4':
      return 'PX4'
    case 'AUTOPILOT_ARDUPILOT':
      return 'ARDU'
    case 'AUTOPILOT_OTHER':
      return 'OTHER'
    default:
      return DASH
  }
}

export function fmtMode(mode: FlightMode | null | undefined, raw: string | undefined): string {
  if (mode === undefined || mode === null) return DASH
  if (mode === 'FLIGHT_MODE_UNKNOWN') return raw ? raw.toUpperCase() : DASH
  return mode.slice('FLIGHT_MODE_'.length).replace('_', ' ')
}

export function fmtPercent(v: number | null | undefined): string {
  if (v === null || v === undefined || !Number.isFinite(v)) return DASH
  return `${Math.round(v)}%`
}

export function fmtFixed(v: number | null | undefined, digits: number): string {
  if (v === null || v === undefined || !Number.isFinite(v)) return DASH
  return v.toFixed(digits)
}

export function fmtHeading(v: number | null | undefined): string {
  if (v === null || v === undefined || !Number.isFinite(v)) return DASH
  return `${(((Math.round(v) % 360) + 360) % 360).toString().padStart(3, '0')}°`
}

export function fmtId(id: number): string {
  return id.toString().padStart(2, '0')
}
