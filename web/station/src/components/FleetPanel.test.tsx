import { cleanup, render, screen, within } from '@testing-library/react'
import { afterEach, expect, test } from 'vitest'
import { FleetPanel } from './FleetPanel'
import { VehicleStateValid } from '../contract/v1'
import { applySample, receiveSample, type FleetState } from '../state/fleetStore'
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

test('a synthetic fleet is marked in the panel header, and a live one is not', () => {
  const fleet = applySample({}, linkMsg(1, 'LINK_CONNECTED'))
  render(<FleetPanel fleet={fleet} stale={false} synthetic />)
  const head = screen.getByRole('heading', { name: 'Fleet' }).parentElement!
  expect(within(head).getByText('SYNTHETIC')).toBeTruthy()
  cleanup()

  render(<FleetPanel fleet={fleet} stale={false} />)
  expect(screen.queryByText('SYNTHETIC')).toBeNull()
})

test('fields whose valid_mask bit is cleared show a dash, not their zero value', () => {
  const fleet = [
    { type: 'sample', topic: 'vehicle_info', data: { ...info, vehicle_id: 4, header: { ...info.header, valid_mask: 0 } }, age_ms: 1 },
    // armed is false and mode is HOLD on the wire, but neither is available.
    {
      type: 'sample', topic: 'vehicle_state', age_ms: 1,
      data: { ...vehicleState, vehicle_id: 4, armed: false, header: { ...vehicleState.header, valid_mask: VehicleStateValid.landed } },
    },
    linkMsg(4, 'LINK_CONNECTED'),
  ].reduce<FleetState>((s, m) => applySample(s, m as SampleMsg), {})
  render(<FleetPanel fleet={fleet} stale={false} />)
  const tr = screen.getAllByRole('row').find((r) => r.dataset.vehicle === '4')!
  const cell = (c: string) => tr.querySelector(`.${c}`)?.textContent
  expect(cell('c-ap')).toBe('—')
  expect(cell('c-mode')).toBe('—')
  expect(cell('c-arm')).toBe('—')
  expect(within(tr).queryByText('DISARMED')).toBeNull()
  expect(within(tr).getByText('CONNECTED')).toBeTruthy()
})

test('a vehicle whose link_status stopped coming reads STALE, in the row and the tally', () => {
  let fleet = receiveSample({}, linkMsg(1, 'LINK_CONNECTED'), 0)
  fleet = receiveSample(fleet, linkMsg(2, 'LINK_CONNECTED'), 2_000)
  render(<FleetPanel fleet={fleet} stale={false} nowMs={3_500} />)
  const rows = screen.getAllByRole('row').filter((r) => r.dataset.vehicle)
  expect(within(rows[0]!).getByText('STALE')).toBeTruthy()
  expect(rows[0]!.closest('tbody')?.dataset.link).toBe('STALE')
  expect(within(rows[1]!).getByText('CONNECTED')).toBeTruthy()
  const tally = screen.getByRole('list', { name: 'Link states' })
  expect(within(tally).getByText('1 STALE')).toBeTruthy()
  expect(within(tally).getByText('1 CONNECTED')).toBeTruthy()
})
