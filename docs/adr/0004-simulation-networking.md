# 0004. Simulation networking and the PX4 image

- Status: Accepted
- Date: 2026-11-23

## Context

The development and benchmark environment is four PX4 v1.17.0 SIH quadrotors.
I want each vehicle in its own container and network namespace, because in M4
I will attach one link emulator per vehicle to that vehicle's namespace.
`network_mode: host` would put all four vehicles in one namespace, so I ruled it
out. The vehicles sit on a user-defined bridge network, `skyrelay-sim`.

PX4 sends its offboard MAVLink link from UDP `14580+N` to `127.0.0.1:14540+N`
(`px4-rc.mavlink`). Inside a container, `127.0.0.1` is the container itself,
so nothing reaches the host unless I change the partner address. PX4 logs that
MAVLink stays on localhost until `MAV_{i}_BROADCAST` is set.

Docker Hub has no `px4io/px4-sitl:v1.17.0` tag. The repository holds 1.17
snapshots (`v1.17.0-alpha1-...`) and 1.18 release candidates, and the v1.17.0
GitHub release has no SITL assets. The Git tag `v1.17.0` and the build image
`px4io/px4-dev:v1.17.0` do exist.

## Decision

**Partner override.** Each service gets
`extra_hosts: ["host.docker.internal:host-gateway"]`. When the image is built,
the Dockerfile patches only the offboard line of `px4-rc.mavlink` to pass
`-t $SKYRELAY_HOST_IP`, and fails the build if that line ever changes. At
start, the entrypoint reads the IPv4 entry for `host.docker.internal` from
`/etc/hosts`, refuses to start without one, and exports it as
`SKYRELAY_HOST_IP`. The remote port is already `14540+N` because the instance
index is N. I tested it:
`check_heartbeats.py` sees sysids 1 to 4 with autopilot 12 on ports 14540 to
14543. Docker resolved `host-gateway` to `172.17.0.1`, the `docker0` address,
and not the `skyrelay-sim` bridge address; the host answers on both.

**Rejected: `MAV_{i}_BROADCAST=1`.** The parameter exists per MAVLink
instance, so it could be set for the offboard link alone, but it does not name
a partner: PX4 then sends to the broadcast address of the container's subnet.
The host would get those datagrams only because it sits on that Docker bridge
network as its gateway, not because anything addresses it, and the bridge
could not move to another machine without redoing the networking. I did not
measure it; I rejected it on those grounds.

**Rejected: `network_mode: host`.** One namespace for all four vehicles, so no
per-vehicle link emulation.

**PX4 image built from source.** `sim/px4-sitl/Dockerfile` builds the
`v1.17.0` tag on top of `px4io/px4-dev:v1.17.0` and copies `bin` and `etc` into
`ubuntu:24.04`, with no Gazebo. The runtime image is 193 MB on disk. The
startup logic mirrors the entrypoint of the published `px4-sitl` image, but
limits the override to the offboard link.

## Consequences

- The first `build` compiles PX4. I have the version exactly pinned, and I own
  the Dockerfile that has to keep working when I bump PX4.
- Only the offboard link reaches the host. A GCS link on 14550 is not routed
  out, which is what I want for the bridge.
- Traffic from a container to the host itself is not masqueraded (Docker's
  masquerade rule only covers traffic routed out through another interface),
  so the host sees each vehicle's datagrams from that container's own address
  on `skyrelay-sim`, 172.18.0.x on my machine. Docker hands those addresses out at start in no
  fixed order, so I tell vehicles apart by port and sysid only.
- Anything on the host that binds 14540 to 14543 competes with the bridge. The
  checker binds those ports too, so stop it before starting the bridge.
- Host firewalls that drop container-to-host UDP will break this. I have not
  hit that on Ubuntu 26.04 with Docker 29.
