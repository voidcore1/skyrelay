// One row per vehicle: id, autopilot, link state, mode, armed, battery, and a
// second line with position, heading and link rate.
import type { CSSProperties } from 'react'
import type { FleetState, VehicleView } from '../state/fleetStore'
import { LINK_COLOR, linkLabel, type LinkLabel } from '../state/link'
import { DASH, fmtAutopilot, fmtFixed, fmtHeading, fmtId, fmtMode, fmtPercent } from './format'

const STATE_ORDER: LinkLabel[] = ['CONNECTED', 'DEGRADED', 'RECOVERING', 'LOST', 'UNKNOWN']

const stateStyle = (l: LinkLabel) => ({ '--state': LINK_COLOR[l] }) as CSSProperties

const withUnit = (text: string, unit: string) => (text === DASH ? DASH : `${text}${unit}`)

function Armed({ v }: { v: VehicleView }) {
  if (v.state === undefined) return <span className="dim">{DASH}</span>
  return v.state.armed ? <span className="armed">ARMED</span> : <span className="dim">DISARMED</span>
}

function Row({ v }: { v: VehicleView }) {
  const link = linkLabel(v.link?.state)
  const p = v.pose
  const rx = v.link?.rx_rate_hz
  const rxText = rx === null || rx === undefined ? DASH : Math.round(rx).toString()
  return (
    <tbody className="vehicle" data-link={link} style={stateStyle(link)}>
      <tr data-vehicle={v.id}>
        <th scope="row" className="c-id">
          {fmtId(v.id)}
        </th>
        <td className="c-ap">{fmtAutopilot(v.info?.autopilot)}</td>
        <td className="c-link">
          <span className="badge">{link === 'UNKNOWN' ? 'NO DATA' : link}</span>
        </td>
        <td className="c-mode" title={v.state?.mode_raw || undefined}>
          {fmtMode(v.state?.mode, v.state?.mode_raw)}
        </td>
        <td className="c-arm">
          <Armed v={v} />
        </td>
        <td className="c-bat num">{fmtPercent(v.health?.battery_remaining_pct)}</td>
      </tr>
      <tr className="sub">
        <td colSpan={6}>
          <span>{fmtFixed(p?.lat_deg, 6)}</span> <span>{fmtFixed(p?.lon_deg, 6)}</span>
          <span className="sep">/</span>
          <span className="k">ALT</span> <span>{withUnit(fmtFixed(p?.alt_rel_home_m, 1), 'm')}</span>
          <span className="sep">/</span>
          <span className="k">HDG</span> <span>{fmtHeading(p?.heading_deg)}</span>
          <span className="sep">/</span>
          <span className="k">RX</span> <span>{withUnit(rxText, 'Hz')}</span>
        </td>
      </tr>
    </tbody>
  )
}

export function FleetPanel({ fleet, stale }: { fleet: FleetState; stale: boolean }) {
  const vehicles = Object.values(fleet).sort((a, b) => a.id - b.id)
  const counts = new Map<LinkLabel, number>()
  for (const v of vehicles) {
    const l = linkLabel(v.link?.state)
    counts.set(l, (counts.get(l) ?? 0) + 1)
  }
  return (
    <section className="panel fleet" aria-label="Fleet" data-stale={stale || undefined}>
      <header className="panel-head">
        <h2>Fleet</h2>
        <span className="panel-count">{vehicles.length.toString().padStart(2, '0')} VEH</span>
        <ul className="tally" aria-label="Link states">
          {STATE_ORDER.filter((l) => counts.has(l)).map((l) => (
            <li key={l} style={stateStyle(l)}>
              {counts.get(l)} {l === 'UNKNOWN' ? 'NO DATA' : l}
            </li>
          ))}
        </ul>
      </header>
      {stale && (
        <p className="stale" role="status">
          Gateway connection down. Everything below is stale.
        </p>
      )}
      {vehicles.length === 0 ? (
        <p className="empty">No vehicles yet. Waiting for telemetry from the gateway.</p>
      ) : (
        <table className="fleet-table">
          <thead>
            <tr>
              <th scope="col">ID</th>
              <th scope="col">AP</th>
              <th scope="col">LINK</th>
              <th scope="col">MODE</th>
              <th scope="col">ARM</th>
              <th scope="col" className="num">
                BAT
              </th>
            </tr>
          </thead>
          {vehicles.map((v) => (
            <Row key={v.id} v={v} />
          ))}
        </table>
      )}
    </section>
  )
}
