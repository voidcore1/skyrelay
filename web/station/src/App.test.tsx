import { act, cleanup, fireEvent, render, screen, within } from '@testing-library/react'
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
  vi.useRealTimers()
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

  expect(screen.getByText('Synthetic demo')).toBeTruthy()
  expect(screen.getByText('Synthetic link replay')).toBeTruthy()
  expect(within(screen.getByRole('region', { name: 'Fleet' })).getByText('SYNTHETIC')).toBeTruthy()
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

function startReplayTest() {
  vi.useFakeTimers({ toFake: ['setInterval', 'clearInterval', 'performance'] })
  window.history.replaceState(null, '', '/?demo=1')
  render(<App />)
  const controls = within(screen.getByRole('group', { name: 'Synthetic link replay' }))
  return {
    click: (name: string) => fireEvent.click(controls.getByRole('button', { name })),
    elapsed: () => controls.getByLabelText('Replay elapsed time').textContent,
    advance: (ms: number) => act(() => vi.advanceTimersByTime(ms)),
  }
}

test('replay waits for Play, freezes on Pause, and stops at twelve seconds', () => {
  const { click, elapsed, advance } = startReplayTest()

  advance(5000)
  expect(elapsed()).toBe('0.0 / 12 s')
  click('Play')
  advance(2250)
  click('Pause')
  expect(elapsed()).toBe('2.2 / 12 s')
  advance(9000)
  expect(elapsed()).toBe('2.2 / 12 s')
  click('Play')
  advance(9700)
  expect(elapsed()).toBe('11.9 / 12 s')
  expect(screen.getByText('Synthetic link replay')).toBeTruthy()
  expect(screen.queryByText('Synthetic replay complete')).toBeNull()
  advance(100)
  expect(elapsed()).toBe('12.0 / 12 s')
  expect(screen.getByText('Synthetic replay complete').getAttribute('aria-live')).toBe('polite')
  expect(screen.getByRole('button', { name: 'Play again' })).toBeTruthy()
  expect(vi.getTimerCount()).toBe(0)
  advance(5000)
  expect(elapsed()).toBe('12.0 / 12 s')
  click('Play again')
  expect(elapsed()).toBe('0.0 / 12 s')
  advance(100)
  expect(elapsed()).toBe('0.1 / 12 s')
  expect(openSocket).not.toHaveBeenCalled()
})

test('reset stops a running replay and restores the snapshot and its timing samples', () => {
  const { click, elapsed, advance } = startReplayTest()
  const map = within(screen.getByRole('img', { name: 'Synthetic schematic map' }))
  const hud = within(screen.getByLabelText('Synthetic pose age'))
  const initialPose = map.getByText('01 CONNECTED').closest('g')?.getAttribute('transform')

  click('Play')
  advance(8000)
  expect(map.getByText('01 RECOVERING')).toBeTruthy()
  click('Reset')
  advance(4000)
  expect(elapsed()).toBe('0.0 / 12 s')
  expect(map.getByText('01 CONNECTED').closest('g')?.getAttribute('transform')).toBe(initialPose)
  expect(hud.getByText('n=3')).toBeTruthy()
  expect(hud.getByText('24')).toBeTruthy()
  expect(hud.getByText('86')).toBeTruthy()
  expect(vi.getTimerCount()).toBe(0)
})

test('replay holds the last pose and adds no timing samples while vehicle 01 is lost', () => {
  const { click, advance } = startReplayTest()
  const map = within(screen.getByRole('img', { name: 'Synthetic schematic map' }))
  const hud = within(screen.getByLabelText('Synthetic pose age'))
  const pose = (label: string) => map.getByText(label).closest('g')?.getAttribute('transform')
  const initialPose = pose('01 CONNECTED')

  click('Play')
  advance(4000)
  const lastPose = pose('01 DEGRADED')
  expect(lastPose).not.toBe(initialPose)
  expect(hud.getByText('n=7')).toBeTruthy()
  advance(1000)
  expect(pose('01 LOST')).toBe(lastPose)
  screen.getByRole('img', { name: 'Synthetic schematic map', description: /vehicle 01.*lost its link.*last reported position/i })
  advance(2000)
  expect(pose('01 LOST')).toBe(lastPose)
  expect(hud.getByText('n=7')).toBeTruthy()
  advance(1000)
  expect(pose('01 RECOVERING')).not.toBe(lastPose)
  expect(hud.getByText('n=8')).toBeTruthy()
  screen.getByRole('img', { name: 'Synthetic schematic map', description: /vehicle 01 is recovering/i })
  advance(2000)
  expect(map.getByText('01 CONNECTED')).toBeTruthy()
})

test('replay expires the snapshot timing samples at exactly ten seconds', () => {
  const { click, advance } = startReplayTest()
  const hud = within(screen.getByLabelText('Synthetic pose age'))

  click('Play')
  advance(9000)
  expect(hud.getByText('n=9')).toBeTruthy()
  advance(999)
  expect(hud.getByText('n=9')).toBeTruthy()
  advance(1)
  expect(hud.getByText('n=7')).toBeTruthy()
})

test.each(['/', '/?demo=0', '/?demo'])('%s uses the normal gateway by default', (url) => {
  window.history.replaceState(null, '', url)
  render(<App />)
  expect(screen.getByText('GATEWAY')).toBeTruthy()
  expect(screen.getByLabelText('Pose age')).toBeTruthy()
  expect(screen.queryByRole('group', { name: 'Synthetic link replay' })).toBeNull()
  expect(screen.queryByText(/synthetic/i)).toBeNull()
  expect(openSocket).toHaveBeenCalledExactlyOnceWith(`ws://${window.location.host}/ws`)
})
