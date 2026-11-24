import { cleanup, render, screen, within } from '@testing-library/react'
import { afterEach, beforeEach, expect, test, vi } from 'vitest'
import App from './App'

// The live renderer requires WebGL. Keep the demo map and gateway client real.
vi.mock('./map/MapView', () => ({ MapView: () => null }))

const openSocket = vi.fn(function () {
  return { close() {} }
})

beforeEach(() => {
  openSocket.mockClear()
  vi.stubGlobal('WebSocket', openSocket)
})

afterEach(() => {
  cleanup()
  window.history.replaceState(null, '', '/')
  vi.unstubAllGlobals()
})

test('explicit demo URL renders the synthetic fleet, map and pose ages without a socket', async () => {
  window.history.replaceState(null, '', '/?demo=1')
  render(<App />)

  const fleet = await screen.findByRole('table')
  expect(within(fleet).getAllByRole('rowheader').map((row) => row.textContent)).toEqual(['01', '02', '03'])
  expect(within(fleet).getByText('DEGRADED')).toBeTruthy()
  expect(within(fleet).getByText('LOST')).toBeTruthy()
  expect(screen.queryByRole('status')).toBeNull()

  const map = screen.getByRole('img', { name: /synthetic schematic map/i })
  expect(within(map).getByText('Landing pad')).toBeTruthy()
  expect(within(map).getByText('02 DEGRADED').getAttribute('fill')).toBe('#ffb000')
  expect(within(map).getAllByText(/^0[1-3] (CONNECTED|DEGRADED|LOST)$/).map((label) => label.textContent))
    .toEqual(['01 CONNECTED', '02 DEGRADED', '03 LOST'])

  expect(screen.getByText(/read-only demo/i)).toBeTruthy()
  const hud = screen.getByLabelText('Synthetic pose age')
  expect(within(hud).getByText('24')).toBeTruthy()
  expect(within(hud).getByText('86')).toBeTruthy()
  expect(within(hud).queryByText(/rtt/i)).toBeNull()
  expect(openSocket).not.toHaveBeenCalled()
})

test('schematic map describes the fixed snapshot and its landmarks', () => {
  window.history.replaceState(null, '', '/?demo=1')
  render(<App />)

  screen.getByRole('img', {
    name: 'Synthetic schematic map',
    description: /north is up.*river bend.*west.*training field.*east.*service strip.*landing pad.*01.*northeast.*02.*southwest.*03.*northwest/i,
  })
})

test('schematic map plots the degraded vehicle at its longitude, latitude and heading', () => {
  window.history.replaceState(null, '', '/?demo=1')
  render(<App />)

  const map = screen.getByRole('img', { name: 'Synthetic schematic map' })
  const label = within(map).getByText('02 DEGRADED')
  const marker = label.closest('g')
  const [, x, y] = marker?.getAttribute('transform')?.match(/^translate\((\S+) (\S+)\)$/) ?? []
  expect(Number(x)).toBeCloseTo(500)
  expect(Number(y)).toBeCloseTo(170)
  expect(marker?.querySelector('path')?.getAttribute('transform')).toBe('rotate(210)')
})

test.each(['/', '/?demo=0', '/?demo'])('%s uses the normal gateway by default', (url) => {
  window.history.replaceState(null, '', url)
  render(<App />)
  expect(screen.getByText('GATEWAY')).toBeTruthy()
  expect(screen.getByLabelText('Pose age')).toBeTruthy()
  expect(openSocket).toHaveBeenCalledExactlyOnceWith(`ws://${window.location.host}/ws`)
})
