import { expect, test } from 'vitest'
import { DIMMED_OPACITY, toFeatureCollection, vehicleLayer, vehicleOpacity } from './vehicleLayer'
import { applySample, receiveSample } from '../state/fleetStore'
import { linkMsg, pose, poseMsg } from '../test/fixtures'

test('vehicles without valid position are not drawn', () => {
  expect(toFeatureCollection({ 1: { id: 1, pose: { ...pose, lat_deg: null } } }).features).toHaveLength(0)
})

test('vehicles without a pose, or with a position off the globe, are not drawn', () => {
  expect(toFeatureCollection({ 1: { id: 1 } }).features).toHaveLength(0)
  expect(toFeatureCollection({ 1: { id: 1, pose: { ...pose, lon_deg: null } } }).features).toHaveLength(0)
  expect(toFeatureCollection({ 1: { id: 1, pose: { ...pose, lat_deg: 91 } } }).features).toHaveLength(0)
})

test('a drawn vehicle is a point with its id, heading and link state', () => {
  const fleet = [poseMsg(2, 47.4, 8.55), linkMsg(2, 'LINK_DEGRADED')].reduce(applySample, {})
  const fc = toFeatureCollection(fleet)
  expect(fc.features).toHaveLength(1)
  const f = fc.features[0]!
  expect(f.geometry).toEqual({ type: 'Point', coordinates: [8.55, 47.4] })
  expect(f.properties).toMatchObject({ id: 2, heading: 90, link: 'DEGRADED' })
})

test('unknown heading draws north, unknown link state draws as unknown', () => {
  const fc = toFeatureCollection({ 4: { id: 4, pose: { ...pose, vehicle_id: 4, heading_deg: null } } })
  expect(fc.features[0]!.properties).toMatchObject({ id: 4, heading: 0, link: 'UNKNOWN' })
})

test('every link state maps to its short name', () => {
  const states = ['LINK_CONNECTED', 'LINK_DEGRADED', 'LINK_LOST', 'LINK_RECOVERING'] as const
  const fleet = states.map((s, i) => [poseMsg(i + 1, 47.4, 8.55), linkMsg(i + 1, s)]).flat().reduce(applySample, {})
  const links = toFeatureCollection(fleet).features.map((f) => f.properties.link)
  expect(links).toEqual(['CONNECTED', 'DEGRADED', 'LOST', 'RECOVERING'])
})

test('a stale vehicle stays at its last position, labelled STALE and dimmed', () => {
  let fleet = applySample({}, poseMsg(3, 47.4, 8.55))
  fleet = receiveSample(fleet, linkMsg(3, 'LINK_CONNECTED'), 1_000)
  expect(toFeatureCollection(fleet, 2_000).features[0]!.properties.link).toBe('CONNECTED')
  const f = toFeatureCollection(fleet, 4_500).features[0]!
  expect(f.properties.link).toBe('STALE')
  expect(f.geometry).toEqual({ type: 'Point', coordinates: [8.55, 47.4] })
  // Per vehicle by label, or all of them while the gateway connection is down.
  expect(vehicleOpacity(false)).toEqual(['case', ['==', ['get', 'link'], 'STALE'], DIMMED_OPACITY, 1])
  expect(vehicleOpacity(true)).toBe(DIMMED_OPACITY)
  expect(vehicleLayer().paint?.['icon-opacity']).toEqual(vehicleOpacity(false))
})
