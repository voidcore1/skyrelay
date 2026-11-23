#!/usr/bin/env python3
"""Wait for MAVLink heartbeats on a set of UDP ports (stdlib only).

Binds one UDP socket per port, parses HEARTBEAT frames (MAVLink v1 or v2) and
exits 0 once every port has seen a heartbeat and the sysids are all distinct.
"""
import argparse
import select
import socket
import sys
import time
from typing import NamedTuple, Optional

HEARTBEAT_MSGID = 0
V2_MAGIC, V1_MAGIC = 0xFD, 0xFE
V2_HEADER, V1_HEADER = 10, 6
HEARTBEAT_LEN = 9  # custom_mode u32, type, autopilot, base_mode, system_status, mavlink_version


class Heartbeat(NamedTuple):
    sysid: int
    compid: int
    autopilot: int
    mav_type: int


def parse_heartbeat(frame: bytes) -> Optional[Heartbeat]:
    """Return the Heartbeat in a single MAVLink frame, or None. The CRC is not checked."""
    if len(frame) < 2:
        return None
    if frame[0] == V2_MAGIC:
        if len(frame) < V2_HEADER:
            return None
        length, sysid, compid = frame[1], frame[5], frame[6]
        msgid = frame[7] | frame[8] << 8 | frame[9] << 16
        payload = frame[V2_HEADER:V2_HEADER + length]
    elif frame[0] == V1_MAGIC:
        if len(frame) < V1_HEADER:
            return None
        length, sysid, compid, msgid = frame[1], frame[3], frame[4], frame[5]
        payload = frame[V1_HEADER:V1_HEADER + length]
    else:
        return None
    if msgid != HEARTBEAT_MSGID or len(payload) != length:
        return None
    payload = payload.ljust(HEARTBEAT_LEN, b"\x00")  # v2 trims trailing zero bytes
    return Heartbeat(sysid, compid, payload[5], payload[4])


def parse_ports(spec: str) -> list[int]:
    ports: list[int] = []
    for part in spec.split(","):
        lo, _, hi = part.partition("-")
        first, last = int(lo), int(hi or lo)
        if last < first:
            raise ValueError(f"empty port range: {part}")
        ports.extend(range(first, last + 1))
    return ports


def wait_for_heartbeats(ports: list[int], timeout: float) -> dict[int, Heartbeat]:
    socks: dict[socket.socket, int] = {}
    seen: dict[int, Heartbeat] = {}
    try:
        for port in ports:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind(("0.0.0.0", port))
            socks[s] = port
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            ready, _, _ = select.select(list(socks), [], [], 0.5)
            for s in ready:
                data, _ = s.recvfrom(2048)
                hb = parse_heartbeat(data)
                if hb is not None and hb.autopilot != 8:  # ignore anything but vehicle autopilots
                    seen.setdefault(socks[s], hb)
            if len(seen) == len(ports) and len({h.sysid for h in seen.values()}) == len(ports):
                break
    finally:
        for s in socks:
            s.close()
    return seen


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ports", default="14540-14543", help="e.g. 14540-14543 or 14540,14541")
    ap.add_argument("--timeout", type=float, default=60.0, help="seconds to wait")
    args = ap.parse_args(argv)
    ports = parse_ports(args.ports)
    seen = wait_for_heartbeats(ports, args.timeout)

    print("port sysid autopilot")
    for port in ports:
        if port in seen:
            print(f"{port} {seen[port].sysid} {seen[port].autopilot}")
    missing = [p for p in ports if p not in seen]
    if missing:
        print(f"no heartbeat on ports: {', '.join(map(str, missing))}", file=sys.stderr)
        return 1
    if len({h.sysid for h in seen.values()}) != len(ports):
        print("sysids are not distinct", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
