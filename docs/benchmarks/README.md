# Benchmarks

Every number SkyRelay quotes comes from `bench/run.sh`, and the raw output of
each published run (the probe's CSV, the report, `host.txt` and the bridge
log) is kept here, in a directory named after the report.

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

Both timestamps are `CLOCK_REALTIME` on one host. A negative latency would mean
the clocks disagree, so such samples are counted, the report gives their number
(the samples themselves stay in the CSV), and any at all fail the gate. All
samples delivered in one `take()` batch share one probe timestamp, which can
only overstate their latency. The seq-gap column counts drops the probe saw
between its first and last received sample of each vehicle.

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
the report's: 1 if the overall p99 is above 100 ms, any sample has a negative
latency, or nothing was received.
Results land in `bench/results/<UTC date>T<HHMMSS>Z-<scenario>/` (git-ignored): the CSV,
`report.md`, `host.txt` and the bridge log.

## Nightly in CI

The `sitl-nightly` workflow (`.github/workflows/sitl-nightly.yml`) runs the same
scenario end to end on a GitHub-hosted `ubuntu-24.04` runner. It runs every
night at 02:00 UTC, on demand from the Actions tab, and on pull requests that
touch `apps/`, `lib/`, `sim/`, `bench/`, `config/`, `idl/`, `cmake/`,
`CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`,
`vcpkg-configuration.json`, `.github/actions/` or the workflow itself. The
job:

1. builds the `release` preset, setting vcpkg up and caching its binary
   archives the same way as the main CI, but in a cache of its own, since it
   builds on the runner itself rather than in the main CI's containers. That
   cache is saved even when the build fails, so a failed cold run still keeps
   the ports it managed to build;
2. builds the PX4 image (`docker compose -f sim/sih-px4-4/compose.yaml build`);
3. runs `BUILD_DIR=build/release bench/run.sh m1-clean`, so the job fails
   exactly when the gate does.

The report is added to the run's summary page. Everything under
`bench/results/` (CSV, `report.md`, `host.txt`, bridge log) is uploaded as the
artifact `m1-clean-results` and kept for 30 days, whether the gate passed or
not. Download it from the run's page, or with
`gh run download <run-id> -n m1-clean-results`.

Nightly numbers are a regression check, not published results. A shared CI
runner is noisier than a quiet machine, and it is a different host from the one
in the published reports. Published results come from a clean tree on a
described host, as listed below.

## Published results

| Date | Scenario | Overall p50 / p95 / p99 | Samples | Raw output |
|---|---|---|---|---|
| 2026-11-23 | [m1-clean](2026-11-23-m1-clean.md) | 0.118 / 0.203 / 0.260 ms | 4658 | [2026-11-23-m1-clean/](2026-11-23-m1-clean/) |
