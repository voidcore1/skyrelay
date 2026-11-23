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


HB_V2 = bytes.fromhex("fd09000000030100000000000000020c510403" + "0000")
OTHER_V2 = bytes.fromhex("fd0000000001010000210000")  # msgid 33, empty payload


def heartbeats_in(datagram):
    found = [check_heartbeats.parse_heartbeat(f) for f in check_heartbeats.iter_frames(datagram)]
    return [h for h in found if h is not None]


class IterFrames(unittest.TestCase):
    def test_heartbeat_after_other_frame(self):
        hbs = heartbeats_in(OTHER_V2 + HB_V2)
        self.assertEqual([h.sysid for h in hbs], [3])

    def test_signed_v2_frame_then_heartbeat(self):
        signed = bytearray(OTHER_V2)
        signed[2] = 0x01  # incompat flag: signed
        signed = bytes(signed) + bytes(13)  # 13-byte signature
        frames = list(check_heartbeats.iter_frames(signed + HB_V2))
        self.assertEqual(len(frames), 2)
        self.assertEqual(len(frames[0]), len(signed))
        self.assertEqual([h.sysid for h in heartbeats_in(signed + HB_V2)], [3])

    def test_v1_then_v2(self):
        v1 = bytes.fromhex("fe09000101" "00" "00000000" "020c5104" "03" "0000")
        self.assertEqual(len(heartbeats_in(v1 + HB_V2)), 2)

    def test_truncated_trailing_frame(self):
        self.assertEqual(len(heartbeats_in(HB_V2 + HB_V2[:12])), 1)
        self.assertEqual(list(check_heartbeats.iter_frames(HB_V2[:5])), [])

    def test_garbage_stops_walk(self):
        self.assertEqual(len(heartbeats_in(HB_V2 + b"\x00\x01" + HB_V2)), 1)
        self.assertEqual(list(check_heartbeats.iter_frames(b"")), [])


class ParsePorts(unittest.TestCase):
    def test_range_and_list(self):
        self.assertEqual(check_heartbeats.parse_ports("14540-14543"), [14540, 14541, 14542, 14543])
        self.assertEqual(check_heartbeats.parse_ports("14540,14542"), [14540, 14542])

    def test_bad_range(self):
        with self.assertRaises(ValueError):
            check_heartbeats.parse_ports("14543-14540")


if __name__ == "__main__":
    unittest.main()
