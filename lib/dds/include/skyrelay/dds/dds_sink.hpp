#pragma once

#include <cstdint>
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

 private:
  template <class T>
  struct Port {
    ::dds::topic::Topic<T> topic;
    ::dds::pub::DataWriter<T> writer;
    std::mutex mu;  // guards seq, and keeps seq order equal to write order
    std::map<uint32_t, uint64_t> seq;

    Port(::dds::domain::DomainParticipant&, ::dds::pub::Publisher&,
         std::string_view name);
    template <class Fn>
    void publish(uint32_t vid, Fn&& to_sample);
  };

  ::dds::pub::Publisher pub_;
  Port<v1::VehicleInfo> info_;
  Port<v1::VehicleState> state_;
  Port<v1::Pose> pose_;
  Port<v1::Health> health_;
  Port<v1::LinkStatus> link_;
};

}  // namespace skyrelay::dds
