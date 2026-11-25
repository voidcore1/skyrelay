import { describe, expect, it } from 'vitest'
import { linkLabel } from '../state/link'
import { DASH, fmtAutopilot, fmtMode } from './format'

// The gateway sends null for an enum value this build does not know.
describe('enum fields that are null', () => {
  it('render as unknown', () => {
    expect(fmtAutopilot(null)).toBe(DASH)
    expect(fmtMode(null, 'AUTO')).toBe(DASH)
    expect(linkLabel(null)).toBe('UNKNOWN')
  })
})
