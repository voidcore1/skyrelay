import { expect, test } from 'vitest'
import { toFeatureCollection } from './vehicleLayer'
import { applySample } from '../state/fleetStore'
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
