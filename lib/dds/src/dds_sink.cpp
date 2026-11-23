#include "skyrelay/dds/dds_sink.hpp"

#include <spdlog/spdlog.h>

#include <string>

#include "skyrelay/dds/mapping.hpp"
#include "skyrelay/dds/qos.hpp"
#include "skyrelay/dds/topics.hpp"

namespace skyrelay::dds {

template <class T>
DdsTelemetrySink::Port<T>::Port(::dds::domain::DomainParticipant& dp,
                                ::dds::pub::Publisher& pub,
                                std::string_view name)
    : name(name),
      topic(dp, std::string(name), topic_qos(profile_for(name))),
      writer(pub, topic, writer_qos(profile_for(name))) {}

// A stuck writer would fail on every sample, so only log the 1st, 2nd, 4th,
// 8th... drop of a topic; the counter keeps the full number.
template <class T>
void DdsTelemetrySink::Port<T>::drop(const char* what) noexcept {
  const uint64_t n = ++dropped;
  if ((n & (n - 1)) == 0) {
    try {
      spdlog::warn("dropped sample on {} ({} so far): {}", name, n, what);
    } catch (...) {
    }
  }
}

template <class T>
DdsTelemetrySink::Slot& DdsTelemetrySink::Port<T>::slot(uint32_t vid) {
  std::lock_guard lock(map_mu);
  return slots[vid];
}

// Only the vehicle's own slot is locked across seq and write. The sample is
// built before that, so string work never sits under a lock. seq moves only
// after a successful write, so a dropped sample leaves no gap for readers.
template <class T>
template <class Make>
void DdsTelemetrySink::Port<T>::publish(uint32_t vid, Make&& make) noexcept {
  try {
    T sample = make();
    Slot& sl = slot(vid);
    std::lock_guard lock(sl.mu);
    sample.header().seq(sl.seq);
    writer.write(sample);
    ++sl.seq;
  } catch (const std::exception& e) {
    drop(e.what());
  } catch (...) {
    drop("unknown error");
  }
}

DdsTelemetrySink::DdsTelemetrySink(::dds::domain::DomainParticipant& dp,
                                   const DomainConfig& cfg)
    : pub_(make_publisher(dp, cfg)),
      info_(dp, pub_, kTopicVehicleInfo),
      state_(dp, pub_, kTopicVehicleState),
      pose_(dp, pub_, kTopicPose),
      health_(dp, pub_, kTopicHealth),
      link_(dp, pub_, kTopicLinkStatus) {}

uint64_t DdsTelemetrySink::dropped_samples(std::string_view t) const {
  if (t == kTopicVehicleInfo) return info_.dropped;
  if (t == kTopicVehicleState) return state_.dropped;
  if (t == kTopicPose) return pose_.dropped;
  if (t == kTopicHealth) return health_.dropped;
  if (t == kTopicLinkStatus) return link_.dropped;
  return 0;
}

void DdsTelemetrySink::vehicle_info(uint32_t vid, const vehicle::InfoData& d,
                                    vehicle::Stamp st) {
  info_.publish(vid, [&] { return to_idl(vid, d, st, 0); });
}

void DdsTelemetrySink::vehicle_state(uint32_t vid, const vehicle::StateData& d,
                                     vehicle::Stamp st) {
  state_.publish(vid, [&] { return to_idl(vid, d, st, 0); });
}

void DdsTelemetrySink::pose(uint32_t vid, const vehicle::PoseData& d,
                            vehicle::Stamp st) {
  pose_.publish(vid, [&] { return to_idl(vid, d, st, 0); });
}

void DdsTelemetrySink::health(uint32_t vid, const vehicle::HealthData& d,
                              vehicle::Stamp st) {
  health_.publish(vid, [&] { return to_idl(vid, d, st, 0); });
}

void DdsTelemetrySink::link_status(uint32_t vid,
                                   const vehicle::LinkStatusData& d,
                                   vehicle::Stamp st) {
  link_.publish(vid, [&] { return to_idl(vid, d, st, 0); });
}

}  // namespace skyrelay::dds
