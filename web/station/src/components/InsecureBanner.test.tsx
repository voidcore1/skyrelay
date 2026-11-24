import { cleanup, render, screen } from '@testing-library/react'
import { afterEach, expect, test } from 'vitest'
import { InsecureBanner } from './InsecureBanner'
import type { HelloMsg } from '../net/protocol'

afterEach(cleanup)

const hello: HelloMsg = { type: 'hello', version: '0.1.0-preview', contract: '1.0.0-preview.1', insecure: true, server_time_ns: 1 }

test('shows INSECURE PREVIEW while the gateway says it is insecure, with no way to dismiss it', () => {
  render(<InsecureBanner hello={hello} />)
  expect(screen.getByRole('alert').textContent).toContain('INSECURE PREVIEW')
  expect(screen.queryByRole('button')).toBeNull()
})

test('shows nothing for a secure gateway or before the first hello', () => {
  const { container, rerender } = render(<InsecureBanner hello={{ ...hello, insecure: false }} />)
  expect(container.textContent).toBe('')
  rerender(<InsecureBanner hello={undefined} />)
  expect(container.textContent).toBe('')
})
