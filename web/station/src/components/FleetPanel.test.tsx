import { cleanup, render, screen, within } from '@testing-library/react'
import { afterEach, expect, test } from 'vitest'
import { FleetPanel } from './FleetPanel'
import { applySample } from '../state/fleetStore'
import type { SampleMsg } from '../net/protocol'
import { health, info, linkMsg, poseMsg, vehicleState } from '../test/fixtures'

afterEach(cleanup)

const vehicle = (id: number, extra: SampleMsg[]): SampleMsg[] => [
  { type: 'sample', topic: 'vehicle_info', data: { ...info, vehicle_id: id }, age_ms: 1 },
  poseMsg(id, 47.39, 8.54),
  ...extra,
]

test('one row per vehicle with autopilot, link state, mode, armed and battery', () => {
  const fleet = [
    ...vehicle(2, [
      linkMsg(2, 'LINK_LOST'),
      { type: 'sample', topic: 'vehicle_state', data: { ...vehicleState, vehicle_id: 2, armed: true, mode: 'FLIGHT_MODE_MISSION' }, age_ms: 1 },
      { type: 'sample', topic: 'health', data: { ...health, vehicle_id: 2, battery_remaining_pct: 81.4 }, age_ms: 1 },
    ]),
    ...vehicle(1, [linkMsg(1, 'LINK_CONNECTED')]),
  ].reduce(applySample, {})
  render(<FleetPanel fleet={fleet} stale={false} />)

  const rows = screen.getAllByRole('row').filter((r) => r.dataset.vehicle)
  expect(rows.map((r) => r.dataset.vehicle)).toEqual(['1', '2'])
  const v2 = within(rows[1]!)
  expect(v2.getByText('PX4')).toBeTruthy()
  expect(v2.getByText('LOST')).toBeTruthy()
  expect(v2.getByText('MISSION')).toBeTruthy()
  expect(v2.getByText('ARMED')).toBeTruthy()
  expect(v2.getByText('81%')).toBeTruthy()
  expect(within(rows[0]!).getByText('CONNECTED')).toBeTruthy()
})

test('fields nobody has reported yet show a dash, not a guess', () => {
  const fleet = applySample({}, poseMsg(7, 47.39, 8.54))
  render(<FleetPanel fleet={fleet} stale={false} />)
  const row = screen.getAllByRole('row').find((r) => r.dataset.vehicle === '7')!
  expect(within(row).getByText('NO DATA')).toBeTruthy()
  expect(within(row).queryByText('ARMED')).toBeNull()
  expect(within(row).queryByText('SAFE')).toBeNull()
})

test('a stale panel says so', () => {
  const fleet = applySample({}, linkMsg(1, 'LINK_CONNECTED'))
  render(<FleetPanel fleet={fleet} stale={true} />)
  expect(screen.getByText(/stale/i)).toBeTruthy()
})
