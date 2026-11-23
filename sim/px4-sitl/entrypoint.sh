#!/bin/sh
# Start PX4 instance $INSTANCE with the SIH quadrotor, headless.
#
# PX4 sends its offboard MAVLink link to 127.0.0.1:14540+N, which stays inside
# the container's network namespace. If host.docker.internal resolves, I point
# that one link at the host instead (see docs/adr/0004-simulation-networking.md).
set -e

PX4_PREFIX=/opt/px4
N="${INSTANCE:-0}"

# mavlink only parses IPv4 literals, so resolve to an IPv4 address first.
HOST_IP=$(getent ahostsv4 host.docker.internal 2>/dev/null | awk '/STREAM/ {print $1; exit}')
if [ -n "$HOST_IP" ]; then
    sed -i "s/-m onboard -o \$udp_offboard_port_remote/-m onboard -t $HOST_IP -o \$udp_offboard_port_remote/" \
        "$PX4_PREFIX/etc/init.d-posix/px4-rc.mavlink"
    echo "skyrelay: offboard MAVLink for instance $N -> $HOST_IP:$((14540 + N))"
fi

# -d: no interactive pxh shell. The positional argument is the data (ROMFS) path.
WORKDIR="/var/lib/px4/$N"
mkdir -p "$WORKDIR"
exec "$PX4_PREFIX/bin/px4" -i "$N" -d -w "$WORKDIR" "$PX4_PREFIX/etc"
