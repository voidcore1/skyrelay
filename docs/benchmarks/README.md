# Benchmarks

Every number SkyRelay quotes comes from `bench/run.sh`, and the raw output of
each published run is kept here.

## What the M1 metric measures

The M1 latency is **bridge decode → consumer**: the time from the moment the
bridge decoded a MAVLink message (`t_bridge_ns`, `CLOCK_REALTIME`) to the moment
a DDS subscriber's `on_data_available` ran for the resulting `pose` sample
(`t_probe_ns`, `CLOCK_REALTIME`, same host, same clock).

It does **not** include the vehicle, the radio or network link, or the time the
datagram spent in the kernel before the bridge read it. It is not an
end-to-end, sensor-to-consumer figure. The method that captures the vehicle side
(design §10) arrives with the link emulators in M4. Until then, read these
numbers as the cost the bridge and DDS add, and nothing more.

The percentile is nearest-rank everywhere: `sorted[ceil(q·n) − 1]`.

## Running it

```sh
cmake --preset dev && cmake --build --preset dev
bench/run.sh m1-clean
```

`run.sh` uses the binaries in `build/dev/`; set `BUILD_DIR` (for example
`BUILD_DIR=build/asan`) to use another tree. It needs Docker access, brings up
the four-vehicle simulation, starts the bridge, waits for all four links, records
`pose` for 60 s, prints the report, and tears everything down. Its exit code is
the report's: 1 if the overall p99 is above 100 ms or nothing was received.
Results land in `bench/results/<UTC date>-<scenario>/` (git-ignored): the CSV,
`report.md`, `host.txt` and the bridge log.

## Published results

| Date | Scenario | Overall p50 / p95 / p99 | Samples |
|---|---|---|---|
| 2026-11-23 | [m1-clean](2026-11-23-m1-clean.md) | 0.085 / 0.169 / 0.212 ms | 4649 |
