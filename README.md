# SkyRelay

SkyRelay is a read-only fleet telemetry dashboard and portfolio project. The
web station shows vehicle positions on a map, summarizes fleet and link state,
and displays recent telemetry timing.

## Standalone browser demo

The standalone demo runs entirely in the browser. It opens on a snapshot
of three synthetic vehicles on a schematic map, with fleet link states and
synthetic pose-age statistics. It runs without a gateway, simulator, or
connected vehicle.

Select **Play** to replay vehicle 01's link over 12 seconds. It starts
connected, degrades at 3 seconds, loses its link at 5, begins recovering at 8,
and reconnects at 10. During the outage, the map holds the last reported pose
and the HUD receives no new pose-age samples. **Pause** freezes replay time,
and **Reset** restores the opening snapshot. Playback stops at 12 seconds.

Use Node.js 24. Run these commands from the repository root.

```sh
cd web/station
npm ci
npm run build
npm run preview -- --host 127.0.0.1 --port 4173 --strictPort
```

Open `http://127.0.0.1:4173/?demo=1`.

![SkyRelay dashboard with three synthetic vehicles](docs/demo/standalone-dashboard.png)

## How the dashboard gets its data

The bridge decodes MAVLink telemetry and publishes typed samples over DDS. The
gateway subscribes to those topics and streams JSON batches to the browser
over the same-origin `/ws` WebSocket. The browser keeps the latest sample for
each vehicle and topic to update the map and fleet panel. The timing display
shows a rolling 10-second p50 and p99 for pose age from bridge decode to gateway
send, alongside a browser-to-gateway round-trip estimate.

If the gateway connection drops after telemetry has arrived, the dashboard
keeps the last received fleet data, dims the table and map markers, and shows a
stale-data notice while it retries. The notice and dimming clear when the socket
reopens. Before the first samples arrive, the fleet panel shows a waiting state.

## Local benchmark

The M1 clean profile measured **0.260 ms overall p99** from bridge decode to
the DDS consumer. This was one 60-second run on one host, with four simulated
vehicles, no link impairment, and DDS over loopback. The measurement covers
only that segment and excludes the vehicle link, gateway, and browser.
[M1 clean benchmark (2026-11-23)](docs/benchmarks/2026-11-23-m1-clean.md)

## Docs

- [Landscape research (Nov 2026)](docs/research/landscape-2026-11.md)

## License

[Apache-2.0](LICENSE)
