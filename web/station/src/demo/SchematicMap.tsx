import { fmtId } from '../components/format'
import { toFeatureCollection } from '../map/vehicleLayer'
import type { FleetState } from '../state/fleetStore'
import { LINK_COLOR, linkLabel } from '../state/link'
import { REPLAY_VEHICLE_ID, SCHEMATIC_BOUNDS } from './snapshot'

const WIDTH = 800
const HEIGHT = 600

export function SchematicMap({ fleet }: { fleet: FleetState }) {
  const { west, east, north, south } = SCHEMATIC_BOUNDS
  const { features } = toFeatureCollection(fleet)
  const replayLink = linkLabel(fleet[REPLAY_VEHICLE_ID]?.link?.state)
  const replayDescription = replayLink === 'LOST'
    ? 'has lost its link. Its last reported position was just east of the river bend, heading northeast.'
    : `is ${replayLink.toLowerCase()}, just east of the river bend and heading northeast.`

  return (
    <div className="map-wrap schematic-wrap">
      <svg
        className="map schematic"
        viewBox={`0 0 ${WIDTH} ${HEIGHT}`}
        role="img"
        aria-label="Synthetic schematic map"
        aria-describedby="schematic-description"
      >
        <desc id="schematic-description">
          North is up. The river bend is on the west side and the training field is to the east.
          The service strip crosses the southern part of the field, and the landing pad is southeast of the strip.
          Vehicle {fmtId(REPLAY_VEHICLE_ID)} {replayDescription}
          Vehicle 02 is degraded, in the northern part of the field and heading southwest.
          Vehicle 03 has lost its link, in the southern part of the field northwest of the landing pad.
          Its last recorded heading was northwest.
        </desc>
        <defs>
          <pattern id="schematic-grid" width="100" height="100" patternUnits="userSpaceOnUse">
            <path d="M 100 0 H 0 V 100" fill="none" stroke="#32383f" />
          </pattern>
        </defs>
        <rect width={WIDTH} height={HEIGHT} fill="#1c2227" />
        <path d="M 0 95 Q 120 92 160 144 T 215 275 Q 204 358 270 414 T 284 600 H 111 Q 141 504 95 471 T 52 367 Q 72 220 0 222 Z" fill="#233e4c" />
        <path d="M 316 76 H 708 V 395 H 535 V 520 H 316 Z" fill="#272f2b" stroke="#66726a" strokeDasharray="6 6" />
        <rect width={WIDTH} height={HEIGHT} fill="url(#schematic-grid)" />
        <path d="M 353 363 H 664 M 535 363 V 476 H 642" fill="none" stroke="#6a746e" strokeWidth="2" />
        <rect x="546" y="349" width="118" height="28" fill="#424a48" stroke="#7a8480" />
        <path d="M 556 363 H 654" stroke="#a7b1ac" strokeDasharray="7 5" />
        <rect x="642" y="457" width="38" height="38" fill="#424a48" stroke="#7a8480" />
        <g className="schematic-labels">
          <g className="schematic-landmarks">
            <text x="31" y="322">River bend</text>
            <text x="335" y="104">Training field</text>
            <text x="546" y="332">Service strip</text>
            <text x="642" y="517">Landing pad</text>
          </g>
          <text className="schematic-north" x="754" y="72" textAnchor="middle">N</text>
          <path d="M 754 81 V 109 M 746 91 L 754 81 L 762 91" fill="none" stroke="currentColor" strokeWidth="2" />
        </g>
        {features.map(({ geometry, properties: { id, heading, link } }) => {
          const [lon, lat] = geometry.coordinates
          const x = (lon! - west) * WIDTH / (east - west)
          const y = (north - lat!) * HEIGHT / (north - south)
          const color = LINK_COLOR[link]
          return (
            <g key={id} transform={`translate(${x} ${y})`}>
              <path
                d="M 0 -13 L 9 9 L 0 5 L -9 9 Z"
                transform={`rotate(${heading})`}
                fill={link === 'LOST' ? '#1c2227' : color}
                stroke={color}
                strokeWidth="2"
              />
              <text className="schematic-vehicle" x="16" y="5" fill={link === 'LOST' ? '#ff7878' : color}>
                {fmtId(id)} {link}
              </text>
            </g>
          )
        })}
      </svg>
      <div className="map-tag" aria-hidden="true">OFFLINE MAP · SYNTHETIC</div>
    </div>
  )
}
