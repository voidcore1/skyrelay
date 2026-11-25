import { fmtId } from '../components/format'
import { LINK_COLOR, type LinkLabel } from '../state/link'
import { REPLAY_SECONDS, REPLAY_VEHICLE_ID } from './snapshot'

interface ReplayControlsProps {
  elapsedMs: number
  playing: boolean
  ended: boolean
  link: LinkLabel
  onToggle: () => void
  onReset: () => void
}

export function ReplayControls({ elapsedMs, playing, ended, link, onToggle, onReset }: ReplayControlsProps) {
  const seconds = (Math.floor(elapsedMs / 100) / 10).toFixed(1)
  const vehicle = fmtId(REPLAY_VEHICLE_ID)

  return (
    <div className="demo-replay" role="group" aria-label="Synthetic link replay">
      <div className="demo-replay-readout">
        <span className="demo-replay-label" aria-live="polite" aria-atomic="true">
          {ended ? 'Synthetic replay complete' : 'Synthetic link replay'}
        </span>
        <span className="demo-replay-state" aria-live="polite" aria-atomic="true" style={{ color: LINK_COLOR[link] }}>
          {vehicle} {link}
        </span>
        <time className="demo-replay-time" aria-label="Replay elapsed time" dateTime={`PT${seconds}S`}>
          {seconds} / {REPLAY_SECONDS} s
        </time>
      </div>
      <div className="demo-replay-buttons">
        <button type="button" className="demo-replay-toggle" onClick={onToggle}>
          {playing ? 'Pause' : ended ? 'Play again' : 'Play'}
        </button>
        <button type="button" onClick={onReset}>Reset</button>
      </div>
    </div>
  )
}
