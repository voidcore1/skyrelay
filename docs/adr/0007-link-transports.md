# 0007. One link interface for UDP, TCP and serial

- Status: Accepted
- Date: 2026-11-25

## Context

M1 had one transport, `UdpLink`, and the session runner used it directly. Real
setups also reach an autopilot over a TCP port (a simulator, a proxy or a
companion computer) or a serial device (a telemetry radio or a flight
controller's USB port), and the runner should not care which one it has.

## Decision

I will put every transport behind one `Link` interface in
`lib/mavlink/include/skyrelay/mav/link.hpp`: `send`, `counters` and
`local_port`, with received chunks delivered to an `RxHandler`. `open_link`
builds the link a URI names, and the session runner holds only a `Link`.

- **Rx handler returns `bool`.** A chunk is a datagram or whatever one stream
  read returned. The handler returns true when the chunk held a frame from the
  locked vehicle (`VehicleSession::on_bytes` now says so).
- **Reply peer from accepted datagrams.** A UDP listen link used to reply to
  whoever sent last, so any stray sender on the port could take the link over.
  It now sets its peer only from a datagram the handler accepted.
- **TCP is a client only.** The bridge connects out; it never listens. A failed
  connect or a lost connection is retried after 500 ms, doubling to 5 s and
  reset after a success. A send while disconnected is dropped and counted, and
  so is a send when more than 64 KiB is already waiting for a peer that stopped
  reading. Queued bytes are discarded when the connection drops, because
  stale MAVLink traffic is worse than none.
- **Serial uses Boost.Asio's `serial_port`**, which opens the device raw; I set
  8N1, no flow control and the URI's baud rate (from a fixed list). A read or
  write error closes the device and it is reopened with the same backoff. A
  failed open is counted in `rx_errors`.
- **Neither TCP nor serial fails at startup** when the peer is not there yet.
  Only a UDP bind that cannot happen throws, as before. The link monitor shows
  the vehicle as lost until it turns up.
- **Literal IP addresses only.** `parse_link_uri` rejects a hostname, so a
  configuration that names one fails at load time instead of at runtime, and no
  link needs a resolver round trip.
- **Same lifetime safety everywhere.** Each link keeps all handler state in a
  `shared_ptr` with an `alive` flag, so destroying a link before its
  `io_context` has drained is safe, and a stale completion from an earlier
  connection is ignored. The rx handler is moved to a local while it runs, so a
  handler may destroy its own link.

## Consequences

Config now accepts `tcp://ip:port` and `serial:///dev/path:baud`, and rejects a
duplicate TCP peer or serial device like it rejects a duplicate UDP port. A
mistyped serial device path is not an error at startup; it shows up as a lost
link and a growing `rx_errors` count. TCP server mode and hostnames are left
out until someone needs them.
