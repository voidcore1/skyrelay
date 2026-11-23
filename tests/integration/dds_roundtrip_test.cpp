#include <gtest/gtest.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>

#include "skyrelay/dds/domain.hpp"
#include "skyrelay/dds/qos.hpp"
#include "skyrelay/dds/topics.hpp"
#include "skyrelay/dds/idl.hpp"

extern char** environ;

using namespace skyrelay::dds;
namespace v1 = skyrelay::v1;
using namespace std::chrono_literals;

namespace {

constexpr const char* kChildEnv = "SKYRELAY_DDS_CHILD_WRITER";

struct Reader {
  ::dds::domain::DomainParticipant dp;
  ::dds::sub::Subscriber sub;
  ::dds::topic::Topic<v1::Pose> topic;
  ::dds::sub::DataReader<v1::Pose> reader;

  Reader(const DomainConfig& c, std::string_view name = kTopicPose)
      : dp(open_participant(c)),
        sub(make_subscriber(dp, c)),
        topic(dp, std::string(name), topic_qos(Profile::TelemetryFast)),
        reader(sub, topic, reader_qos(Profile::TelemetryFast)) {}

  // Polls up to the timeout for any sample with valid data.
  bool take_one(std::chrono::milliseconds timeout, v1::Pose& out) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
      auto samples = reader.take();
      for (const auto& s : samples) {
        if (s.info().valid()) {
          out = s.data();
          return true;
        }
      }
      std::this_thread::sleep_for(10ms);
    }
    return false;
  }
};

v1::Pose pose(uint32_t id, double lat) {
  v1::Pose p;
  p.vehicle_id(id);
  p.lat_deg(lat);
  return p;
}

}  // namespace

TEST(DdsRoundTrip, PoseOnSlashTopicName) {
  DomainConfig c;
  Reader r(c);
  auto dp = open_participant(c);
  auto pub = make_publisher(dp, c);
  ::dds::topic::Topic<v1::Pose> topic(dp, std::string(kTopicPose),
                                      topic_qos(Profile::TelemetryFast));
  ::dds::pub::DataWriter<v1::Pose> writer(pub, topic,
                                          writer_qos(Profile::TelemetryFast));

  v1::Pose got;
  bool ok = false;
  const auto end = std::chrono::steady_clock::now() + 2s;
  while (!ok && std::chrono::steady_clock::now() < end) {
    writer.write(pose(3, 47.0));
    ok = r.take_one(100ms, got);
  }
  ASSERT_TRUE(ok);
  EXPECT_EQ(got.vehicle_id(), 3u);
  EXPECT_DOUBLE_EQ(got.lat_deg(), 47.0);
}

TEST(DdsRoundTrip, PartitionsIsolate) {
  DomainConfig a;
  a.partition = "a";
  DomainConfig b;
  b.partition = "b";
  Reader r(b);
  auto dp = open_participant(a);
  auto pub = make_publisher(dp, a);
  ::dds::topic::Topic<v1::Pose> topic(dp, std::string(kTopicPose),
                                      topic_qos(Profile::TelemetryFast));
  ::dds::pub::DataWriter<v1::Pose> writer(pub, topic,
                                          writer_qos(Profile::TelemetryFast));
  const auto end = std::chrono::steady_clock::now() + 1s;
  v1::Pose got;
  bool any = false;
  while (std::chrono::steady_clock::now() < end && !any) {
    writer.write(pose(3, 47.0));
    any = r.take_one(100ms, got);
  }
  EXPECT_FALSE(any);
}

// Helper run in a child process by CrossProcess below. Without the env var it
// does nothing, so a plain ctest run skips it.
TEST(DdsRoundTrip, ChildWriter) {
  if (!std::getenv(kChildEnv)) GTEST_SKIP() << "only runs as a child process";
  DomainConfig c;
  auto dp = open_participant(c);
  auto pub = make_publisher(dp, c);
  ::dds::topic::Topic<v1::Pose> topic(dp, std::string(kTopicPose),
                                      topic_qos(Profile::TelemetryFast));
  ::dds::pub::DataWriter<v1::Pose> writer(pub, topic,
                                          writer_qos(Profile::TelemetryFast));
  for (int i = 0; i < 100; ++i) {
    writer.write(pose(9, 12.5));
    std::this_thread::sleep_for(50ms);
  }
}

TEST(DdsRoundTrip, CrossProcessDiscoveryOnLoopback) {
  if (std::getenv(kChildEnv)) GTEST_SKIP() << "child process";
  DomainConfig c;
  Reader r(c);

  setenv(kChildEnv, "1", 1);
  char self[4096];
  const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
  ASSERT_GT(n, 0);
  self[n] = '\0';
  std::string filter = "--gtest_filter=DdsRoundTrip.ChildWriter";
  char* argv[] = {self, filter.data(), nullptr};
  pid_t pid = 0;
  ASSERT_EQ(posix_spawn(&pid, self, nullptr, nullptr, argv, environ), 0);
  unsetenv(kChildEnv);

  v1::Pose got;
  const bool ok = r.take_one(8s, got);
  kill(pid, SIGTERM);
  int status = 0;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(ok);
  EXPECT_EQ(got.vehicle_id(), 9u);
  EXPECT_DOUBLE_EQ(got.lat_deg(), 12.5);
}
