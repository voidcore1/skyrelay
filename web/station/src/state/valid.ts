// valid_mask: one bit per field after the header; a cleared bit means the
// vehicle could not supply the value (docs/contract.md).
import type { Header } from '../contract/v1'

// Whether a field's valid_mask bit is set. Every bit the contract defines is
// below 2^31, which is all a JS bitwise operator sees.
export function isValid(header: Header, bit: number): boolean {
  return (header.valid_mask & bit) !== 0
}
