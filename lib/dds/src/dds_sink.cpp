#include "skyrelay/dds/dds_sink.hpp"

#include <string>

#include "skyrelay/dds/mapping.hpp"
#include "skyrelay/dds/qos.hpp"
#include "skyrelay/dds/topics.hpp"

namespace skyrelay::dds {

template <class T>
DdsTelemetrySink::Port<T>::Port(::dds::domain::DomainParticipant& dp,
                                ::dds::pub::Publisher& pub,
                                std::string_view name)
    : topic(dp, std::string(name), topic_qos(profile_for(name))),
      writer(pub, topic, writer_qos(profile_for(name))) {}

// The lock covers taking the next seq and the write, so two threads publishing
// for the same vehicle cannot swap order between the counter and the wire.
template <class T>
template <class Fn>
void DdsTelemetrySink::Port<T>::publish(uint32_t vid, Fn&& to_sample) {
  std::lock_guard lock(mu);
  const uint64_t n = seq[vid]++;
  writer.write(to_sample(n));
}

DdsTelemetrySink::DdsTelemetrySink(::dds::domain::DomainParticipant& dp,
                                   const DomainConfig& cfg)
    : pub_(make_publisher(dp, cfg)),
      info_(dp, pub_, kTopicVehicleInfo),
      state_(dp, pub_, kTopicVehicleState),
      pose_(dp, pub_, kTopicPose),
      health_(dp, pub_, kTopicHealth),
      link_(dp, pub_, kTopicLinkStatus) {}

void DdsTelemetrySink::vehicle_info(uint32_t vid, const vehicle::InfoData& d,
                                    vehicle::Stamp st) {
  info_.publish(vid, [&](uint64_t n) { return to_idl(vid, d, st, n); });
}

void DdsTelemetrySink::vehicle_state(uint32_t vid, const vehicle::StateData& d,
                                     vehicle::Stamp st) {
  state_.publish(vid, [&](uint64_t n) { return to_idl(vid, d, st, n); });
}

void DdsTelemetrySink::pose(uint32_t vid, const vehicle::PoseData& d,
                            vehicle::Stamp st) {
  pose_.publish(vid, [&](uint64_t n) { return to_idl(vid, d, st, n); });
}

void DdsTelemetrySink::health(uint32_t vid, const vehicle::HealthData& d,
                              vehicle::Stamp st) {
  health_.publish(vid, [&](uint64_t n) { return to_idl(vid, d, st, n); });
}

void DdsTelemetrySink::link_status(uint32_t vid,
                                   const vehicle::LinkStatusData& d,
                                   vehicle::Stamp st) {
  link_.publish(vid, [&](uint64_t n) { return to_idl(vid, d, st, n); });
}

}  // namespace skyrelay::dds
