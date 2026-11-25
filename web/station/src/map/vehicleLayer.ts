// Vehicles on the map: one arrow per vehicle at its last known position,
// pointing along its heading and coloured by link state, with the id next to
// it. A lost vehicle's arrow is hollow, a stale one is grey and dimmed, and
// every state other than CONNECTED is also spelled out in the label, so the
// map does not rely on colour alone.
import type { FeatureCollection, Point } from 'geojson'
import type { ExpressionSpecification, SymbolLayerSpecification } from 'maplibre-gl'
import { LINK_COLOR, LINK_SEVERITY, type LinkLabel } from '../state/link'
import { vehicleLink, type FleetState } from '../state/fleetStore'

export interface VehicleProps {
  id: number
  heading: number
  link: LinkLabel
}

export const VEHICLE_SOURCE = 'vehicles'
export const VEHICLE_LAYER = 'vehicles'

const isLat = (v: number | null | undefined): v is number => typeof v === 'number' && Number.isFinite(v) && Math.abs(v) <= 90
const isLon = (v: number | null | undefined): v is number => typeof v === 'number' && Number.isFinite(v) && Math.abs(v) <= 180

// nowMs (performance.now()) lets a vehicle turn STALE when its link_status
// stops coming, as in the fleet panel.
export function toFeatureCollection(f: FleetState, nowMs?: number): FeatureCollection<Point, VehicleProps> {
  const features: FeatureCollection<Point, VehicleProps>['features'] = []
  for (const v of Object.values(f)) {
    const lat = v.pose?.lat_deg
    const lon = v.pose?.lon_deg
    if (!isLat(lat) || !isLon(lon)) continue
    const heading = v.pose?.heading_deg
    features.push({
      type: 'Feature',
      id: v.id,
      geometry: { type: 'Point', coordinates: [lon, lat] },
      properties: {
        id: v.id,
        heading: typeof heading === 'number' && Number.isFinite(heading) ? heading : 0,
        link: vehicleLink(v, nowMs),
      },
    })
  }
  return { type: 'FeatureCollection', features }
}

const links = Object.keys(LINK_COLOR) as LinkLabel[]

function byLink<T extends string | number>(values: Readonly<Record<LinkLabel, T>>, fallback: T): ExpressionSpecification {
  return ['match', ['get', 'link'], ...links.flatMap((l) => [l, values[l]]), fallback] as unknown as ExpressionSpecification
}

// Vehicles can sit close together (the simulated ones start about 45 m
// apart, a few pixels at low zoom), so the labels go into one of four corners
// by id and stay readable when the arrows overlap.
const corner = ['%', ['get', 'id'], 4] as ExpressionSpecification
const idText = ['concat', ['case', ['<', ['get', 'id'], 10], '0', ''], ['to-string', ['get', 'id']]] as ExpressionSpecification

export function vehicleLayer(): SymbolLayerSpecification {
  return {
    id: VEHICLE_LAYER,
    type: 'symbol',
    source: VEHICLE_SOURCE,
    layout: {
      'icon-image': ['concat', 'vehicle-', ['get', 'link']],
      'icon-rotate': ['get', 'heading'],
      'icon-rotation-alignment': 'map',
      'icon-allow-overlap': true,
      'icon-ignore-placement': true,
      'symbol-sort-key': byLink(LINK_SEVERITY, 0),
      'text-field': ['case', ['==', ['get', 'link'], 'CONNECTED'], idText, ['concat', idText, ' ', ['get', 'link']]],
      'text-font': ['Noto Sans Medium'],
      'text-size': 12,
      'text-letter-spacing': 0.08,
      'text-anchor': ['match', corner, 1, 'bottom-left', 2, 'top-left', 3, 'top-right', 'bottom-right'],
      'text-offset': ['match', corner, 1, ['literal', [0.9, -0.5]], 2, ['literal', [0.9, 0.5]], 3, ['literal', [-0.9, 0.5]], ['literal', [-0.9, -0.5]]],
      'text-allow-overlap': true,
      'text-ignore-placement': true,
    },
    paint: {
      'text-color': byLink(LINK_COLOR, LINK_COLOR.UNKNOWN),
      'text-halo-color': '#0a0a0a',
      'text-halo-width': 2,
      'icon-opacity': vehicleOpacity(false),
      'text-opacity': vehicleOpacity(false),
    },
  }
}

export const DIMMED_OPACITY = 0.35

// Stale vehicles are dimmed; when the whole map is stale (the gateway
// connection is down), every vehicle is.
export function vehicleOpacity(allStale: boolean): ExpressionSpecification | number {
  if (allStale) return DIMMED_OPACITY
  return ['case', ['==', ['get', 'link'], 'STALE'], DIMMED_OPACITY, 1] as ExpressionSpecification
}

// Arrow icons, one per link state, drawn once at start-up. Size in CSS pixels.
const ICON_SIZE = 26

export function drawVehicleIcon(link: LinkLabel, pixelRatio: number): ImageData {
  const px = Math.ceil(ICON_SIZE * pixelRatio)
  const canvas = document.createElement('canvas')
  canvas.width = px
  canvas.height = px
  const ctx = canvas.getContext('2d')
  if (!ctx) throw new Error('no 2D canvas context for the vehicle icons')
  ctx.scale(px / 32, px / 32)
  // Pointing north; the layer rotates it by heading.
  ctx.beginPath()
  ctx.moveTo(16, 2.5)
  ctx.lineTo(27, 28.5)
  ctx.lineTo(16, 22)
  ctx.lineTo(5, 28.5)
  ctx.closePath()
  ctx.lineJoin = 'miter'
  if (link === 'LOST') {
    ctx.fillStyle = '#0a0a0a'
    ctx.fill()
    ctx.lineWidth = 3
    ctx.strokeStyle = LINK_COLOR.LOST
    ctx.stroke()
  } else {
    ctx.lineWidth = 3
    ctx.strokeStyle = '#0a0a0a'
    ctx.stroke()
    ctx.fillStyle = LINK_COLOR[link]
    ctx.fill()
  }
  return ctx.getImageData(0, 0, px, px)
}

export const VEHICLE_ICONS = links.map((l) => ({ name: `vehicle-${l}`, link: l }))
