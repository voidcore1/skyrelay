import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "sim"))
import check_heartbeats  # noqa: E402


class ParseHeartbeat(unittest.TestCase):
    def test_v2_heartbeat_frame(self):
        # MAVLink v2 HEARTBEAT, sysid 3, compid 1, autopilot=12 (PX4), type=2 (quadrotor)
        frame = bytes.fromhex("fd09000000030100000000000000020c510403" + "0000")  # CRC bytes ignored by parser
        hb = check_heartbeats.parse_heartbeat(frame)
        self.assertEqual((hb.sysid, hb.compid, hb.autopilot, hb.mav_type), (3, 1, 12, 2))

    def test_rejects_non_heartbeat(self):
        self.assertIsNone(check_heartbeats.parse_heartbeat(bytes.fromhex("fd0000000001010000210000")))

    def test_v2_truncated_payload_is_zero_extended(self):
        # v2 senders trim trailing zero bytes; payload here ends after `type`.
        frame = bytes.fromhex("fd05000000020100000000000000" + "02" + "0000")
        hb = check_heartbeats.parse_heartbeat(frame)
        self.assertEqual((hb.sysid, hb.autopilot, hb.mav_type), (2, 0, 2))

    def test_v1_heartbeat_frame(self):
        frame = bytes.fromhex("fe09000101" "00" "00000000" "020c5104" "03" "0000")
        hb = check_heartbeats.parse_heartbeat(frame)
        self.assertEqual((hb.sysid, hb.compid, hb.autopilot), (1, 1, 12))

    def test_garbage_and_short_input(self):
        self.assertIsNone(check_heartbeats.parse_heartbeat(b""))
        self.assertIsNone(check_heartbeats.parse_heartbeat(b"\xfd\x09"))
        self.assertIsNone(check_heartbeats.parse_heartbeat(b"\x00" * 30))


class ParsePorts(unittest.TestCase):
    def test_range_and_list(self):
        self.assertEqual(check_heartbeats.parse_ports("14540-14543"), [14540, 14541, 14542, 14543])
        self.assertEqual(check_heartbeats.parse_ports("14540,14542"), [14540, 14542])

    def test_bad_range(self):
        with self.assertRaises(ValueError):
            check_heartbeats.parse_ports("14543-14540")


if __name__ == "__main__":
    unittest.main()
