#!/usr/bin/env python3
"""Latency report for the pose benchmark.

Reads the CSV written by skyrelay-bench-probe and reports, per vehicle and
overall, the sample count, the seq gaps and the nearest-rank p50/p95/p99 of
latency_ms = (t_probe_ns - t_bridge_ns) / 1e6.

The percentile is nearest-rank everywhere in SkyRelay: sorted[ceil(q*n) - 1].
"""
import argparse
import csv
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path

QUANTILES = (0.50, 0.95, 0.99)


def nearest_rank(values, q):
    """sorted[ceil(q*n) - 1], or None for no samples."""
    if not values:
        return None
    ordered = sorted(values)
    return ordered[max(math.ceil(q * len(ordered)), 1) - 1]


@dataclass
class VehicleStats:
    latencies_ms: list = field(default_factory=list)
    gaps: int = 0
    last_seq: int | None = None

    def add(self, seq, latency_ms):
        if self.last_seq is not None and seq > self.last_seq:
            self.gaps += seq - self.last_seq - 1
        self.last_seq = seq
        self.latencies_ms.append(latency_ms)


def load(path):
    """Per-vehicle stats, keyed by vehicle_id, rows in file order."""
    stats = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            vid = int(row["vehicle_id"])
            lat = (int(row["t_probe_ns"]) - int(row["t_bridge_ns"])) / 1e6
            stats.setdefault(vid, VehicleStats()).add(int(row["seq"]), lat)
    return stats


def _fmt(x):
    return "n/a" if x is None else f"{x:.3f}"


def _row(label, lats, gaps):
    cells = [_fmt(nearest_rank(lats, q)) for q in QUANTILES]
    return f"| {label} | {len(lats)} | {gaps} | " + " | ".join(cells) + " |"


def render(stats, gate_ms):
    overall = [x for s in stats.values() for x in s.latencies_ms]
    lines = [
        "| vehicle | samples | seq gaps | p50 ms | p95 ms | p99 ms |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for vid in sorted(stats):
        lines.append(_row(str(vid), stats[vid].latencies_ms, stats[vid].gaps))
    lines.append(_row("**overall**", overall, sum(s.gaps for s in stats.values())))
    p99 = nearest_rank(overall, 0.99)
    if gate_ms is None:
        verdict = None
    elif p99 is None:
        verdict = "FAIL: no samples"
    elif p99 > gate_ms:
        verdict = f"FAIL: overall p99 {p99:.3f} ms exceeds {gate_ms:g} ms"
    else:
        verdict = f"PASS: overall p99 {p99:.3f} ms is within {gate_ms:g} ms"
    text = "\n".join(lines) + "\n"
    if verdict:
        text += f"\nGate: {verdict}\n"
    return text, verdict is not None and verdict.startswith("FAIL")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("csv", type=Path)
    ap.add_argument("--out", type=Path, help="write the Markdown report here")
    ap.add_argument("--gate-p99-ms", type=float,
                    help="exit 1 if the overall p99 exceeds this, or there are no samples")
    args = ap.parse_args(argv)
    text, failed = render(load(args.csv), args.gate_p99_ms)
    if args.out:
        args.out.write_text(text)
    sys.stdout.write(text)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
