# Simulation

`sih-px4-4` runs four headless PX4 v1.17.0 quadrotors (SIH physics, no
Gazebo), one container and one network namespace per vehicle, on the
user-defined Docker network `skyrelay-sim`.

## Prerequisites

- Docker with the Compose plugin, and permission to talk to the daemon.
- Python 3.12 or newer for `check_heartbeats.py` (standard library only).
- About 10 GB of free disk for the build environment and the image.

## Up and down

```sh
docker compose -f sim/sih-px4-4/compose.yaml up -d
python3 sim/check_heartbeats.py --ports 14540-14543 --timeout 90
docker compose -f sim/sih-px4-4/compose.yaml down
```

The first `up` builds the image (only `px4-0` has a `build:` section and the other
three wait for it), and every later `up` runs the build step again, which the
layer cache turns into a few seconds.

The checker prints one `port sysid autopilot` row per vehicle and exits 0 when
every port has seen a heartbeat with a distinct sysid. Autopilot 12 is PX4.

## Why the image is built from source

Nobody publishes a headless SITL image for the PX4 v1.17.0 release: the
`px4io/px4-sitl` repository on Docker Hub has no `v1.17.0` tag, only 1.17
snapshots and 1.18 release candidates. `sim/px4-sitl/Dockerfile` therefore
builds the `v1.17.0` tag itself on top of the official `px4io/px4-dev:v1.17.0`
build image and copies the result into a plain `ubuntu:24.04` runtime image.

The first build clones PX4, pulls the multi-gigabyte build image and compiles
everything. It took about three minutes of compile time on a 20-core machine
with the build image already pulled; expect longer on a smaller one or on a slow
connection. Later `up` runs reuse the cached image. To rebuild after changing the
Dockerfile or the entrypoint:

```sh
docker compose -f sim/sih-px4-4/compose.yaml build
# or from scratch
docker compose -f sim/sih-px4-4/compose.yaml build --no-cache
```

## Ports

| Service | Instance | MAVLink sysid | Host UDP port | Home (lat, lon) |
|---|---|---|---|---|
| `px4-0` | 0 | 1 | 14540 | 47.397942, 8.545894 (NE) |
| `px4-1` | 1 | 2 | 14541 | 47.397542, 8.545894 (SE) |
| `px4-2` | 2 | 3 | 14542 | 47.397542, 8.545294 (SW) |
| `px4-3` | 3 | 4 | 14543 | 47.397942, 8.545294 (NW) |

The homes are the corners of a square with sides of about 45 m (22.2 m
north-south and 22.6 m east-west from the centre), centred on PX4's default home
47.397742, 8.545594, so the vehicles do not sit on top of each other on the
station's map. Each service sets `PX4_HOME_LAT` and `PX4_HOME_LON`, which PX4's
`px4-rc.sihsim` turns into the `SIH_LOC_LAT0` and `SIH_LOC_LON0` parameters.
The altitude is the SIH default.

Each container sends its offboard link to the host, so a program on the host
receives it by binding the port. How that works, and why I did not use
broadcast, is in [ADR-0004](../docs/adr/0004-simulation-networking.md).
