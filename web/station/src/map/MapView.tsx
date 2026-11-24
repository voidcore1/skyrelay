// The map. MapLibre owns the canvas; React only hands it the vehicle
// positions. The worker script and the PMTiles protocol are both local, so the
// map makes no request that leaves the gateway.
import 'maplibre-gl/dist/maplibre-gl.css'
import workerUrl from 'maplibre-gl/dist/maplibre-gl-worker.mjs?url'
import {
  AttributionControl,
  type GeoJSONSource,
  Map as MapLibreMap,
  NavigationControl,
  ScaleControl,
  addProtocol,
  removeProtocol,
  setWorkerUrl,
} from 'maplibre-gl'
import { Protocol } from 'pmtiles'
import { useEffect, useRef } from 'react'
import type { FleetState } from '../state/fleetStore'
import { HOME, MAX_BOUNDS, basemapStyle } from './basemap'
import { VEHICLE_ICONS, VEHICLE_LAYER, VEHICLE_SOURCE, drawVehicleIcon, toFeatureCollection, vehicleLayer } from './vehicleLayer'

// MapLibre finds its worker relative to its own module, which is gone once the
// app is bundled, so point it at the copy the build emits.
setWorkerUrl(workerUrl)

const ICON_PIXEL_RATIO = 2
const STALE_OPACITY = 0.35

function setStale(map: MapLibreMap, stale: boolean) {
  const opacity = stale ? STALE_OPACITY : 1
  map.setPaintProperty(VEHICLE_LAYER, 'icon-opacity', opacity)
  map.setPaintProperty(VEHICLE_LAYER, 'text-opacity', opacity)
}

export function MapView({ fleet, stale }: { fleet: FleetState; stale: boolean }) {
  const container = useRef<HTMLDivElement>(null)
  // Set once the vehicle layer exists.
  const ready = useRef<MapLibreMap | null>(null)
  const latest = useRef({ fleet, stale })

  useEffect(() => {
    const protocol = new Protocol()
    addProtocol('pmtiles', protocol.tile)
    const map = new MapLibreMap({
      container: container.current!,
      style: basemapStyle(window.location.origin),
      center: HOME,
      zoom: 15.5,
      minZoom: 9,
      maxZoom: 19,
      maxBounds: MAX_BOUNDS,
      attributionControl: false,
      dragRotate: false,
      pitchWithRotate: false,
    })
    map.touchZoomRotate.disableRotation()
    map.addControl(new AttributionControl({ compact: false }), 'bottom-right')
    map.addControl(new NavigationControl({ showCompass: false }), 'top-right')
    map.addControl(new ScaleControl({ unit: 'metric', maxWidth: 120 }), 'bottom-left')
    map.on('load', () => {
      for (const { name, link } of VEHICLE_ICONS) {
        map.addImage(name, drawVehicleIcon(link, ICON_PIXEL_RATIO), { pixelRatio: ICON_PIXEL_RATIO })
      }
      map.addSource(VEHICLE_SOURCE, { type: 'geojson', data: toFeatureCollection(latest.current.fleet) })
      map.addLayer(vehicleLayer())
      setStale(map, latest.current.stale)
      ready.current = map
    })
    return () => {
      ready.current = null
      map.remove()
      removeProtocol('pmtiles')
    }
  }, [])

  useEffect(() => {
    latest.current.fleet = fleet
    ready.current?.getSource<GeoJSONSource>(VEHICLE_SOURCE)?.setData(toFeatureCollection(fleet))
  }, [fleet])

  useEffect(() => {
    latest.current.stale = stale
    if (ready.current) setStale(ready.current, stale)
  }, [stale])

  return (
    <div className="map-wrap">
      <div ref={container} className="map" />
      <div className="map-tag" aria-hidden="true">
        OFFLINE MAP · PMTILES
      </div>
    </div>
  )
}
