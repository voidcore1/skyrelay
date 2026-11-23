import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "bench" / "report"))
import latency  # noqa: E402

HEADER = "vehicle_id,seq,t_source_us,t_bridge_ns,t_probe_ns\n"


def write_csv(tmp, rows):
    path = Path(tmp) / "pose.csv"
    path.write_text(HEADER + "".join(f"{v},{s},0,{b},{p}\n" for v, s, b, p in rows))
    return path


class Percentiles(unittest.TestCase):
    def test_nearest_rank(self):
        self.assertEqual(latency.nearest_rank([5, 1, 4, 2, 3], 0.50), 3)
        self.assertEqual(latency.nearest_rank(list(range(1, 101)), 0.99), 99)
        self.assertEqual(latency.nearest_rank([7], 0.99), 7)

    def test_empty_is_none(self):
        self.assertIsNone(latency.nearest_rank([], 0.5))


class Report(unittest.TestCase):
    def test_gate_fails_over_100ms(self):
        with tempfile.TemporaryDirectory() as tmp:
            # 98 samples at 10 ms, 2 at 150 ms: p99 = 150 ms.
            rows = [(1, i, 1000, 1000 + (150_000_000 if i < 2 else 10_000_000))
                    for i in range(100)]
            csv = write_csv(tmp, rows)
            out = Path(tmp) / "report.md"
            self.assertEqual(latency.main([str(csv), "--out", str(out), "--gate-p99-ms", "100"]), 1)
            self.assertIn("150.000", out.read_text())

    def test_gate_passes_under_100ms(self):
        with tempfile.TemporaryDirectory() as tmp:
            rows = [(1, i, 5, 5 + 20_000_000) for i in range(50)]
            csv = write_csv(tmp, rows)
            out = Path(tmp) / "report.md"
            self.assertEqual(latency.main([str(csv), "--out", str(out), "--gate-p99-ms", "100"]), 0)

    def test_zero_samples_fails_gate(self):
        with tempfile.TemporaryDirectory() as tmp:
            csv = write_csv(tmp, [])
            out = Path(tmp) / "report.md"
            self.assertEqual(latency.main([str(csv), "--out", str(out), "--gate-p99-ms", "100"]), 1)

    def test_seq_gaps_counted(self):
        with tempfile.TemporaryDirectory() as tmp:
            rows = [(1, 0, 100, 1_000_100), (1, 1, 100, 1_000_100), (1, 3, 100, 1_000_100),
                    (2, 0, 100, 1_000_100), (2, 1, 100, 1_000_100)]
            csv = write_csv(tmp, rows)
            stats = latency.load(csv)
            self.assertEqual(stats[1].gaps, 1)
            self.assertEqual(stats[2].gaps, 0)


if __name__ == "__main__":
    unittest.main()
