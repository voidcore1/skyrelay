import { cleanup, render } from '@testing-library/react'
import { afterEach, expect, test } from 'vitest'
import { LatencyHud } from './LatencyHud'

afterEach(cleanup)

const text = (el: HTMLElement) => el.textContent?.replace(/\s+/g, ' ').trim()

test('shows nearest-rank p50 and p99, the round trip, and that they are indicative', () => {
  const { container } = render(<LatencyHud stats={{ p50: 0.323, p99: 12.6, count: 700 }} rttMs={1.29} />)
  expect(text(container)).toContain('telem p50 0.3ms p99 13ms · rtt 1.3ms · indicative')
})

test('shows dashes when there is nothing to measure', () => {
  const { container } = render(<LatencyHud stats={null} rttMs={undefined} />)
  expect(text(container)).toContain('telem p50 —ms p99 —ms · rtt —ms · indicative')
})
