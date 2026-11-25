#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include "skyrelay/mav/framer.hpp"
#include "skyrelay/mav/packer.hpp"

using namespace skyrelay::mav;

namespace {

void append(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src) {
  dst.insert(dst.end(), src.begin(), src.end());
}

// Random bytes that cannot start a frame, so the expected outcome is
// deterministic.
std::vector<uint8_t> noise(std::mt19937& rng, size_t n) {
  std::vector<uint8_t> v;
  while (v.size() < n) {
    const auto b = static_cast<uint8_t>(rng());
    if (b != MAVLINK_STX && b != MAVLINK_STX_MAVLINK1) v.push_back(b);
  }
  return v;
}

}  // namespace

TEST(Framer, ParsesHeartbeatRoundTrip) {
  Packer p(0, 245, MAV_COMP_ID_MISSIONPLANNER);
  Framer f(1);
  auto out = f.push(Packer::serialize(p.gcs_heartbeat()));
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].msgid, MAVLINK_MSG_ID_HEARTBEAT);
  EXPECT_EQ(out[0].sysid, 245);
  EXPECT_EQ(f.stats().frames_ok, 1u);
}

TEST(Framer, ResyncsAfterGarbage) {
  Packer p(0, 245, MAV_COMP_ID_MISSIONPLANNER);
  Framer f(4);
  std::mt19937 rng(7);

  std::vector<uint8_t> stream = noise(rng, 37);
  // A frame cut off after its first 6 bytes. The parser has already read the
  // length from it, so it swallows the start of the next frame as payload and
  // fails the CRC; that first heartbeat is lost and the parser then resyncs.
  const auto hb = Packer::serialize(p.gcs_heartbeat());
  stream.insert(stream.end(), hb.begin(), hb.begin() + 6);
  for (int i = 0; i < 3; ++i) append(stream, Packer::serialize(p.gcs_heartbeat()));

  auto out = f.push(stream);
  EXPECT_EQ(out.size(), 2u);
  EXPECT_GT(f.stats().bytes_discarded, 0u);
}

TEST(Framer, CountsCrcErrors) {
  Packer p(0, 245, MAV_COMP_ID_MISSIONPLANNER);
  Framer f(5);
  auto bytes = Packer::serialize(p.gcs_heartbeat());
  bytes[10] ^= 0xFF;  // first payload byte
  auto out = f.push(bytes);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(f.stats().crc_errors, 1u);
  EXPECT_EQ(f.stats().frames_ok, 0u);
}

TEST(Framer, AcceptsMavlinkV1Frame) {
  // HEARTBEAT: 9-byte payload, CRC extra 50.
  std::vector<uint8_t> frame = {MAVLINK_STX_MAVLINK1, 9, 0, 1, 1, 0,
                                0, 0, 0, 0,             // custom_mode
                                MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_PX4, 0,
                                MAV_STATE_STANDBY, 3};
  uint16_t crc;
  crc_init(&crc);
  crc_accumulate_buffer(&crc, reinterpret_cast<const char*>(frame.data()) + 1,
                        static_cast<uint16_t>(frame.size() - 1));
  crc_accumulate(50, &crc);
  frame.push_back(static_cast<uint8_t>(crc & 0xFF));
  frame.push_back(static_cast<uint8_t>(crc >> 8));

  Framer f(6);
  auto out = f.push(frame);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].magic, MAVLINK_STX_MAVLINK1);
  EXPECT_EQ(out[0].msgid, MAVLINK_MSG_ID_HEARTBEAT);
}

TEST(Framer, RandomBuffersNeverCrash) {
  Packer p(0, 245, MAV_COMP_ID_MISSIONPLANNER);
  Framer f(7);
  std::mt19937 rng(42);
  for (int i = 0; i < 10000; ++i) {
    std::vector<uint8_t> buf(1 + rng() % 512);
    for (auto& b : buf) b = static_cast<uint8_t>(rng());
    f.push(buf);
  }

  // The framer must not be left stuck mid-frame: after enough clean bytes to
  // flush any partial frame, valid heartbeats parse again.
  std::vector<uint8_t> tail(300, 0);
  f.push(tail);
  std::vector<uint8_t> two;
  append(two, Packer::serialize(p.gcs_heartbeat()));
  append(two, Packer::serialize(p.gcs_heartbeat()));
  auto out = f.push(two);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].msgid, MAVLINK_MSG_ID_HEARTBEAT);
  EXPECT_EQ(out[1].msgid, MAVLINK_MSG_ID_HEARTBEAT);
}
