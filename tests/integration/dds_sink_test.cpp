#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "skyrelay/dds/dds_sink.hpp"
#include "skyrelay/dds/gen/v1_valid_bits.hpp"
#include "skyrelay/dds/domain.hpp"
#include "skyrelay/dds/idl.hpp"
#include "skyrelay/dds/qos.hpp"
#include "skyrelay/dds/topics.hpp"

using namespace skyrelay::dds;
namespace v1 = skyrelay::v1;
using namespace std::chrono_literals;

namespace {

template <class T>
struct TestReader {
  ::dds::sub::Subscriber sub;
  ::dds::topic::Topic<T> topic;
  ::dds::sub::DataReader<T> reader;

  TestReader(::dds::domain::DomainParticipant& dp, const DomainConfig& c,
             std::string_view name, Profile p)
      : sub(make_subscriber(dp, c)),
        topic(dp, std::string(name), topic_qos(p)),
        reader(sub, topic, reader_qos(p)) {}

  // Collects valid samples until `n` have arrived or the timeout passes.
  std::vector<T> collect(size_t n, std::chrono::milliseconds timeout) {
    std::vector<T> out;
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (out.size() < n && std::chrono::steady_clock::now() < end) {
      for (const auto& s : reader.take()) {
        if (s.info().valid()) out.push_back(s.data());
      }
      std::this_thread::sleep_for(10ms);
    }
    return out;
  }
};

// Each test uses its own partition so samples from one test never reach
// another test's reader.
DomainConfig cfg(const char* partition) {
  DomainConfig c;
  c.partition = partition;
  return c;
}

// A reader only receives what is written after it matched, so wait for the
// writer to see it before sending.
void wait_for_match(const std::function<bool()>& matched) {
  const auto end = std::chrono::steady_clock::now() + 3s;
  while (!matched() && std::chrono::steady_clock::now() < end) {
    std::this_thread::sleep_for(10ms);
  }
}

}  // namespace

TEST(DdsSink, SeqIsPerVehiclePerTopic) {
  auto c = cfg("sink-seq");
  auto dp = open_participant(c);
  TestReader<v1::Pose> r(dp, c, kTopicPose, Profile::TelemetryFast);
  TestReader<v1::Health> rh(dp, c, kTopicHealth, Profile::TelemetryReliable);
  DdsTelemetrySink sink(dp, c);
  wait_for_match([&] {
    return ::dds::sub::matched_publications(r.reader).size() > 0 &&
           ::dds::sub::matched_publications(rh.reader).size() > 0;
  });

  skyrelay::vehicle::PoseData d;
  d.lat_deg = 1.0;
  sink.pose(1, d, {.t_source_us = 1, .t_bridge_ns = 2});
  sink.pose(1, d, {});
  sink.pose(2, d, {});
  sink.health(1, {}, {});

  // KEEP_LAST 1 per instance, so a slow reader may see only the newest sample
  // of vehicle 1. Wait for the full set but accept two.
  auto got = r.collect(3, 1s);
  ASSERT_GE(got.size(), 2u);
  std::map<uint32_t, std::vector<uint64_t>> seqs;
  for (const auto& s : got) seqs[s.vehicle_id()].push_back(s.header().seq());
  // Whatever vehicle 1 samples arrived, they end at seq 1.
  EXPECT_EQ(seqs[2], std::vector<uint64_t>{0});
  ASSERT_FALSE(seqs[1].empty());
  EXPECT_EQ(seqs[1].back(), 1u);

  // Health for vehicle 1 has its own counter.
  auto h = rh.collect(1, 3s);
  ASSERT_EQ(h.size(), 1u);
  EXPECT_EQ(h[0].header().seq(), 0u);
}

TEST(DdsSink, StampsAndValidMaskReachReader) {
  auto c = cfg("sink-stamp");
  auto dp = open_participant(c);
  TestReader<v1::Pose> r(dp, c, kTopicPose, Profile::TelemetryFast);
  DdsTelemetrySink sink(dp, c);
  wait_for_match(
      [&] { return ::dds::sub::matched_publications(r.reader).size() > 0; });
  skyrelay::vehicle::PoseData d;
  d.lat_deg = 47.5;
  sink.pose(5, d, {.t_source_us = 11, .t_bridge_ns = 22});
  auto got = r.collect(1, 3s);
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].header().t_source_us(), 11u);
  EXPECT_EQ(got[0].header().t_bridge_ns(), 22u);
  EXPECT_EQ(got[0].header().valid_mask(), v1::valid::Pose::lat_deg);
}

TEST(DdsSink, LateJoinerGetsLatchedLinkStatus) {
  auto c = cfg("sink-latched");
  auto dp = open_participant(c);
  DdsTelemetrySink sink(dp, c);
  skyrelay::vehicle::LinkStatusData d;
  d.state = skyrelay::mav::LinkState::Connected;
  sink.link_status(8, d, {});

  TestReader<v1::LinkStatus> r(dp, c, kTopicLinkStatus, Profile::StateLatched);
  auto got = r.collect(1, 3s);
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].vehicle_id(), 8u);
  EXPECT_EQ(got[0].state(), v1::LinkState::LINK_CONNECTED);
}
