#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <map>
#include <mutex>
#include <utility>

#include "skyrelay/dds/domain.hpp"
#include "skyrelay/dds/idl.hpp"
#include "skyrelay/vehicle/telemetry_sink.hpp"

namespace skyrelay::dds {

// Publishes vehicle telemetry on the five v1 topics with the contract QoS.
// Sequence numbers are per (topic, vehicle_id) and start at 0. Safe to call
// from several session threads at once.
class DdsTelemetrySink final : public vehicle::TelemetrySink {
 public:
  DdsTelemetrySink(::dds::domain::DomainParticipant&, const DomainConfig&);

  void vehicle_info(uint32_t vid, const vehicle::InfoData&,
                    vehicle::Stamp) override;
  void vehicle_state(uint32_t vid, const vehicle::StateData&,
                     vehicle::Stamp) override;
  void pose(uint32_t vid, const vehicle::PoseData&, vehicle::Stamp) override;
  void health(uint32_t vid, const vehicle::HealthData&,
              vehicle::Stamp) override;
  void link_status(uint32_t vid, const vehicle::LinkStatusData&,
                   vehicle::Stamp) override;

 // Samples dropped on a topic because the write (or building the sample)
  // failed. Counted per topic name, e.g. kTopicPose.
  uint64_t dropped_samples(std::string_view topic) const;

 private:
  // One slot per vehicle: its lock serialises seq assignment with the write,
  // so seq reaches the wire in order, without making vehicles wait for each
  // other. std::map nodes are stable, so a slot reference stays valid.
  struct Slot {
    std::mutex mu;
    uint64_t seq = 0;
  };

  template <class T>
  struct Port {
    std::string name;
    ::dds::topic::Topic<T> topic;
    ::dds::pub::DataWriter<T> writer;
    std::mutex map_mu;  // only guards finding or creating a slot
    std::map<uint32_t, Slot> slots;
    std::atomic<uint64_t> dropped{0};

    Port(::dds::domain::DomainParticipant&, ::dds::pub::Publisher&,
         std::string_view name);
    Slot& slot(uint32_t vid);
    void drop(const char* what) noexcept;
    // Never throws. `make` builds the sample for a given seq; a failure drops
    // the sample and does not consume a seq number.
    template <class Make>
    void publish(uint32_t vid, Make&& make) noexcept;
  };

  ::dds::pub::Publisher pub_;
  Port<v1::VehicleInfo> info_;
  Port<v1::VehicleState> state_;
  Port<v1::Pose> pose_;
  Port<v1::Health> health_;
  Port<v1::LinkStatus> link_;
};

}  // namespace skyrelay::dds
