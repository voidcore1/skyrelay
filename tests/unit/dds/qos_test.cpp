#include <gtest/gtest.h>

#include <stdexcept>

#include "skyrelay/dds/qos.hpp"
#include "skyrelay/dds/topics.hpp"

using namespace skyrelay::dds;
using ::dds::core::Duration;
using namespace ::dds::core::policy;

TEST(Qos, TelemetryFastMatchesContract) {
  auto q = writer_qos(Profile::TelemetryFast);
  EXPECT_EQ(q.policy<Reliability>().kind(), ReliabilityKind::RELIABLE);
  EXPECT_EQ(q.policy<History>().kind(), HistoryKind::KEEP_LAST);
  EXPECT_EQ(q.policy<History>().depth(), 1);
  EXPECT_EQ(q.policy<Durability>().kind(), DurabilityKind::VOLATILE);
  EXPECT_EQ(q.policy<Deadline>().period(), Duration::from_millisecs(200));
  EXPECT_EQ(q.policy<Liveliness>().kind(), LivelinessKind::AUTOMATIC);
  EXPECT_EQ(q.policy<Liveliness>().lease_duration(), Duration::from_secs(1));
}

TEST(Qos, TelemetryReliableMatchesContract) {
  auto q = writer_qos(Profile::TelemetryReliable);
  EXPECT_EQ(q.policy<Reliability>().kind(), ReliabilityKind::RELIABLE);
  EXPECT_EQ(q.policy<History>().depth(), 1);
  EXPECT_EQ(q.policy<Durability>().kind(), DurabilityKind::VOLATILE);
  EXPECT_EQ(q.policy<Deadline>().period(), Duration::from_millisecs(1000));
}

TEST(Qos, StateLatchedIsTransientLocal) {
  auto q = writer_qos(Profile::StateLatched);
  EXPECT_EQ(q.policy<Durability>().kind(), DurabilityKind::TRANSIENT_LOCAL);
  EXPECT_EQ(q.policy<Reliability>().kind(), ReliabilityKind::RELIABLE);
  EXPECT_EQ(q.policy<History>().depth(), 1);
}

TEST(Qos, ReaderAndTopicAgreeWithWriter) {
  for (auto p : {Profile::TelemetryFast, Profile::TelemetryReliable,
                 Profile::StateLatched}) {
    auto w = writer_qos(p);
    auto r = reader_qos(p);
    auto t = topic_qos(p);
    EXPECT_EQ(r.policy<Durability>().kind(), w.policy<Durability>().kind());
    EXPECT_EQ(r.policy<Deadline>().period(), w.policy<Deadline>().period());
    EXPECT_EQ(t.policy<Durability>().kind(), w.policy<Durability>().kind());
    EXPECT_EQ(t.policy<Deadline>().period(), w.policy<Deadline>().period());
    EXPECT_EQ(r.policy<History>().depth(), 1);
  }
}

TEST(Qos, ProfileForTopics) {
  EXPECT_EQ(profile_for(kTopicVehicleInfo), Profile::StateLatched);
  EXPECT_EQ(profile_for(kTopicLinkStatus), Profile::StateLatched);
  EXPECT_EQ(profile_for(kTopicPose), Profile::TelemetryFast);
  EXPECT_EQ(profile_for(kTopicVehicleState), Profile::TelemetryReliable);
  EXPECT_EQ(profile_for(kTopicHealth), Profile::TelemetryReliable);
  EXPECT_THROW(profile_for("skyrelay/v1/nope"), std::invalid_argument);
}

TEST(Qos, TopicNamesAreVersioned) {
  EXPECT_EQ(kTopicPose, "skyrelay/v1/pose");
  EXPECT_EQ(kTopicLinkStatus, "skyrelay/v1/link_status");
}

TEST(Qos, EveryProfileIsXcdr2) {
  for (auto p : {Profile::TelemetryFast, Profile::TelemetryReliable,
                 Profile::StateLatched, Profile::Command, Profile::CommandStatus}) {
    const DataRepresentationIdSeq want{DataRepresentationId::XCDR2};
    EXPECT_EQ(writer_qos(p).policy<DataRepresentation>().value(), want);
    EXPECT_EQ(reader_qos(p).policy<DataRepresentation>().value(), want);
    EXPECT_EQ(topic_qos(p).policy<DataRepresentation>().value(), want);
  }
}

TEST(Qos, CommandProfileMatchesContract) {
  auto q = writer_qos(Profile::Command);
  EXPECT_EQ(q.policy<Reliability>().kind(), ReliabilityKind::RELIABLE);
  EXPECT_EQ(q.policy<History>().kind(), HistoryKind::KEEP_ALL);
  EXPECT_EQ(q.policy<ResourceLimits>().max_samples_per_instance(), 64);
  EXPECT_EQ(q.policy<Durability>().kind(), DurabilityKind::VOLATILE);
  EXPECT_EQ(q.policy<Lifespan>().duration(), Duration::from_secs(5));
}

TEST(Qos, CommandStatusProfileMatchesContract) {
  auto q = writer_qos(Profile::CommandStatus);
  EXPECT_EQ(q.policy<Reliability>().kind(), ReliabilityKind::RELIABLE);
  EXPECT_EQ(q.policy<History>().kind(), HistoryKind::KEEP_LAST);
  EXPECT_EQ(q.policy<History>().depth(), 1);
  EXPECT_EQ(q.policy<Durability>().kind(), DurabilityKind::TRANSIENT_LOCAL);
  EXPECT_EQ(q.policy<Lifespan>().duration(), Duration::from_secs(60));
}

TEST(Qos, ReaderAndTopicAgreeForCommandProfiles) {
  for (auto p : {Profile::Command, Profile::CommandStatus}) {
    auto w = writer_qos(p);
    auto r = reader_qos(p);
    auto t = topic_qos(p);
    EXPECT_EQ(r.policy<History>().kind(), w.policy<History>().kind());
    EXPECT_EQ(r.policy<History>().depth(), w.policy<History>().depth());
    EXPECT_EQ(r.policy<Durability>().kind(), w.policy<Durability>().kind());
    EXPECT_EQ(t.policy<History>().kind(), w.policy<History>().kind());
    EXPECT_EQ(t.policy<Durability>().kind(), w.policy<Durability>().kind());
    EXPECT_EQ(t.policy<Lifespan>().duration(), w.policy<Lifespan>().duration());
  }
}

TEST(Qos, ProfileForNewTopics) {
  EXPECT_EQ(profile_for(kTopicMissionState), Profile::StateLatched);
  EXPECT_EQ(profile_for(kTopicCommandRequest), Profile::Command);
  EXPECT_EQ(profile_for(kTopicCommandStatus), Profile::CommandStatus);
}
