# Gateway protocol

`skyrelay-gateway` is the web station's only server. It is a separate DDS
participant that reads the five M1 telemetry topics (see
[contract.md](contract.md)) and serves:

- the station itself, a single-page app, from its web root;
- the offline map data from its data directory;
- a WebSocket that streams telemetry to every connected browser as JSON.

**INSECURE PREVIEW.** There is no TLS and no authentication in M1. The gateway
only listens on loopback (127.0.0.0/8 or `::1`); any other `--listen` address
makes it exit with code 2 and
`refusing non-loopback listen address without TLS (arrives in v0.3)`.

```
skyrelay-gateway --listen 127.0.0.1:8080 --web-root web/station/dist --data-dir data \
                 [--domain 42 --partition default --insecure-allow-network]
```

`--insecure-allow-network` only affects DDS, the same way as for the bridge.
The HTTP side stays on loopback either way.

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
either a single object (`hello` or `pong`) or an array of `sample` objects.**
A frame that is an array never holds anything but samples.

### Server to client

On connect, before anything else:

```json
{"type":"hello","version":"0.1.0-preview","contract":"1.0.0-preview.1","insecure":true,"server_time_ns":1795400000123456789}
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
- The order of samples inside a frame carries no meaning.

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
  every vehicle, like a late-joining DDS reader with the `state_latched` QoS.
  The volatile topics simply arrive with their next sample.

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
