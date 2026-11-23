#pragma once

#include <cstdint>
#include <vector>

#include "skyrelay/vehicle/telemetry_sink.hpp"

namespace skyrelay::test {

template <class T>
struct Recorded {
  uint32_t vehicle_id;
  T data;
  vehicle::Stamp stamp;
};

// Keeps every call a session makes, in order, per topic. Not thread-safe: read
// it from the thread that drives the session, or after that thread has joined.
class RecordingSink final : public vehicle::TelemetrySink {
 public:
  void vehicle_info(uint32_t id, const vehicle::InfoData& d,
                    vehicle::Stamp s) override {
    infos.push_back({id, d, s});
  }
  void vehicle_state(uint32_t id, const vehicle::StateData& d,
                     vehicle::Stamp s) override {
    states.push_back({id, d, s});
  }
  void pose(uint32_t id, const vehicle::PoseData& d,
            vehicle::Stamp s) override {
    poses.push_back({id, d, s});
  }
  void health(uint32_t id, const vehicle::HealthData& d,
              vehicle::Stamp s) override {
    healths.push_back({id, d, s});
  }
  void link_status(uint32_t id, const vehicle::LinkStatusData& d,
                   vehicle::Stamp s) override {
    links.push_back({id, d, s});
  }

  // Link states in the order they were published, with repeats (the periodic
  // publishes) folded away.
  std::vector<mav::LinkState> link_states() const {
    std::vector<mav::LinkState> out;
    for (const auto& l : links) {
      if (out.empty() || out.back() != l.data.state) out.push_back(l.data.state);
    }
    return out;
  }

  std::vector<Recorded<vehicle::InfoData>> infos;
  std::vector<Recorded<vehicle::StateData>> states;
  std::vector<Recorded<vehicle::PoseData>> poses;
  std::vector<Recorded<vehicle::HealthData>> healths;
  std::vector<Recorded<vehicle::LinkStatusData>> links;
};

}  // namespace skyrelay::test
