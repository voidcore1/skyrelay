// Always on while the gateway reports itself insecure; it cannot be closed.
import type { HelloMsg } from '../net/protocol'

export function InsecureBanner({ hello }: { hello: HelloMsg | undefined }) {
  if (!hello?.insecure) return null
  return (
    <div className="insecure" role="alert">
      <strong className="insecure-title">INSECURE PREVIEW</strong>
      <span className="insecure-text">
        No TLS, no authentication. Loopback only; do not expose this station.
      </span>
      <span className="insecure-version">gateway {hello.version}</span>
    </div>
  )
}
