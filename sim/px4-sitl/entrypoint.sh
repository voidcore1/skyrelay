#!/bin/sh
# Start PX4 instance $INSTANCE with the SIH quadrotor, headless.
#
# The image's px4-rc.mavlink sends the offboard link to $SKYRELAY_HOST_IP
# (patched in at build time). Here I resolve host.docker.internal (from /etc/hosts) to that
# IPv4 address and refuse to start if I can't, because PX4 would otherwise send to
# 127.0.0.1 inside the container and nobody would notice.
# See docs/adr/0004-simulation-networking.md.
set -eu

PX4_PREFIX=/opt/px4
N="${INSTANCE:-0}"

die() { echo "skyrelay: error: $*" >&2; exit 1; }

case "$N" in
    [0-9]|1[0-5]) ;;
    *) die "INSTANCE must be an integer 0-15, got '$N'" ;;
esac

# Read the address Compose put in /etc/hosts (extra_hosts) and not DNS: a search
# domain with a wildcard record can make host.docker.internal resolve to a public
# address, and PX4 would then send telemetry to the internet.
# mavlink only parses IPv4 literals.
HOST_IP=$(awk '$2 == "host.docker.internal" && $1 !~ /:/ {print $1; exit}' /etc/hosts)
[ -n "$HOST_IP" ] || die "no IPv4 entry for host.docker.internal in /etc/hosts; add extra_hosts host.docker.internal:host-gateway"
case "$HOST_IP" in
    *[!0-9.]*|*.*.*.*.*.*) die "host.docker.internal is '$HOST_IP', not an IPv4 address" ;;
esac
grep -q -- '-t \$SKYRELAY_HOST_IP -o \$udp_offboard_port_remote' "$PX4_PREFIX/etc/init.d-posix/px4-rc.mavlink" \
    || die "px4-rc.mavlink is missing the host-address patch"
export SKYRELAY_HOST_IP="$HOST_IP"
echo "skyrelay: instance $N offboard MAVLink -> $HOST_IP:$((14540 + N))"

# -d: no interactive pxh shell. The positional argument is the data (ROMFS) path.
WORKDIR="/var/lib/px4/$N"
mkdir -p "$WORKDIR"
exec "$PX4_PREFIX/bin/px4" -i "$N" -d -w "$WORKDIR" "$PX4_PREFIX/etc"
