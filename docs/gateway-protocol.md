# Gateway protocol

`skyrelay-gateway` is the web station's only server. It is a separate DDS
participant that reads the five M1 telemetry topics (see
[contract.md](contract.md)) and serves:

- the station itself, a single-page app, from its web root;
- the offline map data from its data directory;
- a WebSocket that streams telemetry to every connected browser as JSON.

```
skyrelay-gateway --listen 127.0.0.1:8080 --web-root web/station/dist --data-dir data \
                 [--allow-origin http://localhost:5173] \
                 [--domain 42 --partition default --insecure-allow-network]
```

`--insecure-allow-network` only affects DDS, the same way as for the bridge,
and it has to match: give it to every SkyRelay process on the host (bridge,
gateway, `skyrelay-bench-probe`) or to none. A participant in the default
loopback configuration does not discover one that uses the network, so a
gateway without the flag next to a bridge with it shows no vehicles.

## Security in the preview

**INSECURE PREVIEW.** There is no TLS and no authentication in M1; both arrive
in M3 (v0.3). Until then the gateway relies on three checks.

- **It listens on loopback only** (127.0.0.0/8 or `::1`). Any other `--listen`
  address makes it exit with code 2 and
  `refusing non-loopback listen address without TLS (arrives in M3, v0.3)`. That
  keeps other machines out, but not web pages running in the operator's own
  browser, which is what the next two checks are for.
- **Host check, on every request.** The `Host` header must name loopback with
  the port the gateway actually listens on: a 127.0.0.0/8 literal, `[::1]` or
  `localhost` (case-insensitive), such as `127.0.0.1:8080` or
  `localhost:8080`. A `Host` without a port counts as port 80. Anything else,
  or no `Host` at all, gets `403`. This stops DNS rebinding, where a page on
  some domain re-points its own name at 127.0.0.1 and then reads the gateway
  as if it were its own site; its requests still carry its own name in `Host`.
- **Origin check, on the WebSocket.** Browsers do not apply the same-origin
  policy to WebSockets, so without this any page I visit could open
  `ws://127.0.0.1:8080/ws` and read live positions. A `/ws` upgrade is accepted
  only when `Origin` is absent (not a browser: tools, scripts, tests) or is
  the gateway's own origin, `http://127.0.0.1:<port>`,
  `http://localhost:<port>` or `http://[::1]:<port>`, or is given with
  `--allow-origin`. Anything else gets `403` before the upgrade. Origins are
  compared as exact strings after lower-casing the scheme and host, so
  `http://localhost:5173/` (with a slash) or another port is a different
  origin.

`--allow-origin` can be repeated, and it takes `http://` or `https://` plus host
and optional port, nothing else. Its use is the station's development server:
run the gateway with `--allow-origin http://localhost:5173`, and have the dev
server's proxy rewrite `Host` to the gateway's (`changeOrigin: true` in Vite),
or the Host check refuses the proxied requests.

An open WebSocket that goes quiet is not kept forever: the gateway pings it
when it has been idle for a while and closes it if nothing at all has arrived
for 300 s.

## HTTP

| Route | Serves |
|---|---|
| `GET/HEAD /…` | The station from the web root. A missing path whose last segment has no dot is a client-side route and gets `index.html`; a missing file with a dot gets 404. |
| `GET/HEAD /tiles/…` | `<data-dir>/tiles`, 404 if missing, never `index.html` |
| `GET/HEAD /assets/…` | `<data-dir>/assets` (glyphs, sprites), 404 if missing, never `index.html` |
| `GET/HEAD /healthz` | `200`, `text/plain`, body `ok` |
| `GET /ws` | The WebSocket below. Without an upgrade it answers 426. |

The request path is percent-decoded before it is used. A path with a `..`
segment (also an escaped one such as `%2e%2e` or `..%2f`), a NUL byte or a
broken escape gets 400, and so does a path that resolves outside its directory
through a symlink. Other methods get 405.

### Range requests

MapLibre reads the PMTiles archive with HTTP Range requests, so these have to
be right or the map stays blank without an error.

- Every file response carries `Accept-Ranges: bytes` and `Content-Length`.
- A single range (`bytes=a-b`, `bytes=a-`, `bytes=-n`) gets `206` with
  `Content-Range: bytes a-b/size`. An end past the file is clamped to the
  last byte; a suffix longer than the file is the whole file.
- A range that starts at or past the end, or `bytes=-0`, gets `416` with
  `Content-Range: bytes */size`.
- Several ranges get the whole file with `200`, which RFC 9110 allows; nothing
  in the station asks for more than one.
- A malformed `Range`, a unit other than `bytes`, or a request with `If-Range`
  also gets the whole file with `200`.
- `HEAD` ignores `Range` (range handling is only defined for `GET`) and
  answers like a full `GET`, without the body.

Content types come from the extension: `.pmtiles` is
`application/octet-stream`, `.pbf` `application/x-protobuf`, and `.json`,
`.png`, `.js`, `.css`, `.html` and `.svg` get their usual types.

## WebSocket `/ws`

Every message is one JSON text frame. **What the browser receives is always
either a single object (`hello` or `pong`) or an array of `sample` and `gone`
objects.** A frame that is an array never holds anything else.

### Server to client

On connect, before anything else:

```json
{"type":"hello","version":"0.1.0-preview","contract":"1.0.0-preview.2","insecure":true,"server_time_ns":1795400000123456789}
```

`server_time_ns` is the gateway's `CLOCK_REALTIME`. `insecure` is always true in
M1; the station shows the INSECURE PREVIEW banner while it is.

Telemetry comes as arrays of samples:

```json
[{"type":"sample","topic":"pose","data":{"vehicle_id":2,"header":{"seq":41,"t_source_us":0,"t_bridge_ns":1795400000111111111,"valid_mask":3},"lat_deg":47.397742,"lon_deg":8.545594,"…":"…"},"age_ms":12.346},
 {"type":"sample","topic":"health","data":{"vehicle_id":2,"…":"…"},"age_ms":3.020}]
```

- `topic` is the last part of the DDS topic name: `vehicle_info`,
  `vehicle_state`, `pose`, `health` or `link_status`.
- `data` is the DDS sample converted by the generated `to_json_value`: the
  fields of [contract/v1-fields.md](contract/v1-fields.md), enums as their
  literal name, NaN as `null`.
- `age_ms = (now_realtime_ns − data.header.t_bridge_ns) / 1e6`, computed by
  the gateway when it sends that sample, so it covers the bridge, DDS, the
  gateway and any time the sample spent waiting for the browser. Both clocks
  are `CLOCK_REALTIME`; with the bridge on another host the value is only as
  good as their synchronisation, and it can be negative. It is `null` when the
  bridge time is unknown (0).
- The order of messages inside a frame carries no meaning.

The station's latency HUD uses the `age_ms` of `pose` only. Pose is published
as each position message is decoded, so its age is bridge + DDS + gateway;
`vehicle_state` and `health` are republished at a fixed rate from the last
decoded message, and the latched topics are replayed to new clients with the
age of the stored sample, so their ages also hold how old the source message
was.

When a vehicle's data stops having a live writer in DDS (the bridge exited,
crashed and its DDS lease ran out, or disposed the vehicle), every client that
was sent something for that topic and vehicle gets:

```json
[{"type":"gone","topic":"link_status","vehicle_id":2}]
```

It means the last sample of that topic for that vehicle is no longer backed by
anything. The station marks a vehicle STALE when its `link_status` is gone (and
also when no `link_status` has arrived for 3 s, since the bridge sends one every
second). It can arrive in the same array as samples, and a later sample for the
same topic and vehicle means the data is back.

### Fan-out rules

Each client has one latest-value slot per (topic, vehicle):

- `pose` is capped at **10 Hz per vehicle per client**. A pose that arrives
  too early is held and replaced by the next one, so a steady 20 Hz stream goes
  out every other sample with no added delay. If nothing replaces a held pose
  within 100 ms (the stream paused or stopped), it is sent anyway, so the
  browser always ends up with the last position.
- Every other topic is forwarded as it arrives.
- A client has **at most one write outstanding**. While it is busy, new
  samples only overwrite its slots; when the write completes, everything that
  changed goes out together as one array. A browser that reads slowly gets
  fewer, fresher frames, and never delays DDS or the other clients. Overwritten
  samples are counted per client and logged when it disconnects.
- On connect a client also gets the last `vehicle_info` and `link_status` of
  every vehicle whose writer is still alive, much as a late-joining DDS reader
  with the `state_latched` QoS gets them from that writer. A value whose writer
  has gone is dropped together with the `gone` notice above, so a stopped
  bridge is never replayed as if it were still running. The volatile topics
  simply arrive with their next sample.
- A `gone` notice uses the same slot as the samples of its topic and vehicle:
  it replaces a sample that has not been sent yet, and a newer sample replaces
  it.

### Client to server

```json
{"type":"ping","id":7,"t_client_ms":1234.5}
```

is answered with

```json
{"type":"pong","id":7,"t_client_ms":1234.5,"server_time_ns":1795400000123456789}
```

`id` and `t_client_ms` are echoed back as sent when they are numbers, and as
`null` otherwise. The station uses this for its round-trip estimate.

Any other client message is ignored and counted; the connection stays up.
Commands arrive in M2. A client message larger than 64 KiB closes the
connection.
