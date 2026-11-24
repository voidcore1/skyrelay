#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "fanout.hpp"
#include "skyrelay/common/clock.hpp"

using namespace skyrelay::gateway;
using skyrelay::FakeClock;
using nlohmann::json;
using namespace std::chrono_literals;

namespace {

// A client whose "socket" just records frames. busy stands in for a write
// still in progress.
struct Recorder {
  std::vector<std::string> frames;
  bool busy = false;

  Fanout::ClientId attach(Fanout& f) {
    return f.add_client([this](std::string s) { frames.push_back(std::move(s)); },
                        [this] { return busy; });
  }

  // Every sample message of every frame, in order. Each frame must be a JSON
  // array of sample objects.
  std::vector<json> samples() const {
    std::vector<json> out;
    for (const auto& f : frames) {
      const json j = json::parse(f);
      EXPECT_TRUE(j.is_array()) << f;
      for (const auto& s : j) out.push_back(s);
    }
    return out;
  }
};

std::string data(int marker) { return json{{"marker", marker}}.dump(); }

}  // namespace

TEST(Fanout, PoseThrottledTo10Hz) {
  // 50 pose publishes over 1 s, writes complete instantly → ≤ 11 pose messages
  // delivered.
  FakeClock clock;
  Fanout f(clock);
  Recorder r;
  r.attach(f);
  for (int i = 0; i < 50; ++i) {
    f.publish("pose", 1, data(i), clock.mono(), clock.realtime_ns());
    clock.advance(20ms);
  }
  const auto got = r.samples();
  EXPECT_LE(got.size(), 11u);
  EXPECT_GE(got.size(), 9u);
  for (const auto& s : got) EXPECT_EQ(s["topic"], "pose");
}

TEST(Fanout, LatestValueWhileWriting) {
  // write_in_progress true; publish 100 poses for vehicle 1 → after completion
  // exactly 1 delivered (the last); dropped_superseded == 99.
  FakeClock clock;
  Fanout f(clock);
  Recorder r;
  r.busy = true;
  const auto id = r.attach(f);
  for (int i = 0; i < 100; ++i) {
    f.publish("pose", 1, data(i), clock.mono(), clock.realtime_ns());
  }
  EXPECT_TRUE(r.frames.empty());
  r.busy = false;
  f.on_write_complete(id, clock.mono());
  ASSERT_EQ(r.frames.size(), 1u);
  const auto got = r.samples();
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0]["data"]["marker"], 99);
  EXPECT_EQ(f.stats(id).dropped_superseded, 99u);
}

TEST(Fanout, OtherTopicsGoOutOnArrival) {
  FakeClock clock;
  Fanout f(clock);
  Recorder r;
  r.attach(f);
  for (int i = 0; i < 50; ++i) {
    f.publish("health", 1, data(i), clock.mono(), clock.realtime_ns());
    clock.advance(20ms);
  }
  EXPECT_EQ(r.frames.size(), 50u);
}

TEST(Fanout, DirtySlotsGoOutAsOneFrame) {
  FakeClock clock;
  clock.set_realtime_ns(1'000'000'000'000);
  Fanout f(clock);
  Recorder r;
  r.busy = true;
  const auto id = r.attach(f);
  const uint64_t t = clock.realtime_ns() - 5'000'000;  // 5 ms before now
  f.publish("pose", 1, data(1), clock.mono(), t);
  f.publish("pose", 2, data(2), clock.mono(), t);
  f.publish("health", 1, data(3), clock.mono(), t);
  EXPECT_TRUE(r.frames.empty());
  r.busy = false;
  f.on_write_complete(id, clock.mono());
  ASSERT_EQ(r.frames.size(), 1u);
  const auto got = r.samples();
  ASSERT_EQ(got.size(), 3u);
  for (const auto& s : got) {
    EXPECT_EQ(s["type"], "sample");
    EXPECT_TRUE(s["data"].is_object());
    EXPECT_DOUBLE_EQ(s["age_ms"].get<double>(), 5.0);
  }
  EXPECT_EQ(got[0]["topic"], "pose");
  EXPECT_EQ(got[2]["topic"], "health");
  // Nothing is left over once the frame went out.
  f.on_write_complete(id, clock.mono());
  EXPECT_EQ(r.frames.size(), 1u);
}

TEST(Fanout, AgeIsTakenWhenTheSampleIsSent) {
  FakeClock clock;
  clock.set_realtime_ns(2'000'000'000'000);
  Fanout f(clock);
  Recorder r;
  r.busy = true;
  const auto id = r.attach(f);
  f.publish("health", 7, data(1), clock.mono(), clock.realtime_ns() - 2'000'000);
  clock.advance(40ms);
  r.busy = false;
  f.on_write_complete(id, clock.mono());
  const auto got = r.samples();
  ASSERT_EQ(got.size(), 1u);
  EXPECT_DOUBLE_EQ(got[0]["age_ms"].get<double>(), 42.0);
}

TEST(Fanout, UnknownBridgeTimeHasNoAge) {
  FakeClock clock;
  Fanout f(clock);
  Recorder r;
  r.attach(f);
  f.publish("health", 1, data(1), clock.mono(), 0);
  const auto got = r.samples();
  ASSERT_EQ(got.size(), 1u);
  EXPECT_TRUE(got[0]["age_ms"].is_null());
}

TEST(Fanout, HeldPoseIsSentWhenTheStreamPauses) {
  FakeClock clock;
  Fanout f(clock);
  Recorder r;
  r.attach(f);
  f.publish("pose", 1, data(1), clock.mono(), 0);  // sent at once
  clock.advance(20ms);
  f.publish("pose", 1, data(2), clock.mono(), 0);  // too early, held
  ASSERT_EQ(r.frames.size(), 1u);
  // Within one period a newer pose may still replace it, so poll leaves it.
  clock.advance(50ms);
  f.poll(clock.mono());
  EXPECT_EQ(r.frames.size(), 1u);
  // A full period with nothing newer: the held pose goes out.
  clock.advance(50ms);
  f.poll(clock.mono());
  ASSERT_EQ(r.frames.size(), 2u);
  EXPECT_EQ(r.samples().back()["data"]["marker"], 2);
  // And only once.
  clock.advance(200ms);
  f.poll(clock.mono());
  EXPECT_EQ(r.frames.size(), 2u);
}

TEST(Fanout, PoseThrottleIsPerVehicle) {
  FakeClock clock;
  Fanout f(clock);
  Recorder r;
  r.attach(f);
  for (uint32_t v = 1; v <= 4; ++v) f.publish("pose", v, data(1), clock.mono(), 0);
  EXPECT_EQ(r.samples().size(), 4u);
}

TEST(Fanout, LatchedTopicsReachNewClients) {
  FakeClock clock;
  Fanout f(clock, {"vehicle_info", "link_status"});
  f.publish("vehicle_info", 3, data(1), clock.mono(), 0);
  f.publish("link_status", 3, data(2), clock.mono(), 0);
  f.publish("link_status", 3, data(3), clock.mono(), 0);
  f.publish("pose", 3, data(4), clock.mono(), 0);
  Recorder r;
  const auto id = r.attach(f);
  // Nothing is sent from inside add_client.
  EXPECT_TRUE(r.frames.empty());
  f.on_write_complete(id, clock.mono());
  // One frame with the latest of each latched topic, in no particular order.
  ASSERT_EQ(r.frames.size(), 1u);
  std::map<std::string, int> markers;
  for (const auto& s : r.samples()) markers[s["topic"]] = s["data"]["marker"];
  EXPECT_EQ(markers, (std::map<std::string, int>{{"vehicle_info", 1}, {"link_status", 3}}));
  EXPECT_EQ(f.stats(id).dropped_superseded, 0u);
}

TEST(Fanout, BusyClientDoesNotHoldUpOthers) {
  FakeClock clock;
  Fanout f(clock);
  Recorder slow;
  slow.busy = true;
  Recorder fast;
  const auto slow_id = slow.attach(f);
  const auto fast_id = fast.attach(f);
  for (int i = 0; i < 100; ++i) {
    f.publish("health", 1, data(i), clock.mono(), 0);
    clock.advance(1ms);
  }
  EXPECT_TRUE(slow.frames.empty());
  EXPECT_EQ(fast.frames.size(), 100u);
  EXPECT_EQ(f.stats(slow_id).dropped_superseded, 99u);
  EXPECT_EQ(f.stats(fast_id).dropped_superseded, 0u);
}

TEST(Fanout, RemovedClientGetsNothing) {
  FakeClock clock;
  Fanout f(clock);
  Recorder r;
  const auto id = r.attach(f);
  f.remove_client(id);
  f.publish("health", 1, data(1), clock.mono(), 0);
  f.poll(clock.mono());
  EXPECT_TRUE(r.frames.empty());
  EXPECT_EQ(f.stats(id).dropped_superseded, 0u);
}
