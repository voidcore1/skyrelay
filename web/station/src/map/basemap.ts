// The offline basemap: Protomaps vector tiles from the gateway's data
// directory (tools/maps/fetch-region.sh puts them there), with glyphs and
// sprites from the same place. Nothing here points off this machine except the
// attribution link.
import { layers, namedFlavor } from '@protomaps/basemaps'
import type { LngLatBoundsLike, LngLatLike, StyleSpecification } from 'maplibre-gl'

// PX4's default home, where the simulated vehicles start.
export const HOME: LngLatLike = [8.545594, 47.397742]

// The extract covers 8.40,47.30 to 8.70,47.48; panning stops a little beyond.
export const MAX_BOUNDS: LngLatBoundsLike = [
  [8.3, 47.24],
  [8.8, 47.54],
]

export const OSM_ATTRIBUTION = '<a href="https://www.openstreetmap.org/copyright" target="_blank" rel="noreferrer">© OpenStreetMap</a>'

// MapLibre insists on an absolute sprite URL, so the origin comes in here.
export function basemapStyle(origin: string): StyleSpecification {
  return {
    version: 8,
    glyphs: `${origin}/assets/fonts/{fontstack}/{range}.pbf`,
    sprite: `${origin}/assets/sprites/v4/dark`,
    sources: {
      protomaps: {
        type: 'vector',
        url: 'pmtiles:///tiles/region.pmtiles',
        attribution: OSM_ATTRIBUTION,
      },
    },
    // Without points of interest: shops and parks only add noise here, and
    // their green, orange and blue labels would compete with the link colours.
    layers: layers('protomaps', namedFlavor('dark'), { lang: 'en' }).filter((l) => l.id !== 'pois'),
  }
}
