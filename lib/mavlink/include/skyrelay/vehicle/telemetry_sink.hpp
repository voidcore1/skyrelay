#pragma once

#include <cstdint>

#include "skyrelay/mav/link_monitor.hpp"
#include "skyrelay/vehicle/types.hpp"

namespace skyrelay::vehicle {

// The two timestamps every sample carries. t_source_us is the vehicle's own
// UTC time (0 if unknown), t_bridge_ns is CLOCK_REALTIME at decode.
struct Stamp {
  uint64_t t_source_us = 0;
  uint64_t t_bridge_ns = 0;
};

struct LinkStatusData {
  mav::LinkState state = mav::LinkState::Lost;
  uint64_t state_since_ns = 0;
  mav::LinkStats stats{};
};

// Where a vehicle session sends what it knows. The session does not care
// whether that ends up on DDS or in a test double, so lib/mavlink stays free
// of any DDS types.
class TelemetrySink {
 public:
  virtual ~TelemetrySink() = default;
  virtual void vehicle_info(uint32_t vehicle_id, const InfoData&, Stamp) = 0;
  virtual void vehicle_state(uint32_t vehicle_id, const StateData&, Stamp) = 0;
  virtual void pose(uint32_t vehicle_id, const PoseData&, Stamp) = 0;
  virtual void health(uint32_t vehicle_id, const HealthData&, Stamp) = 0;
  virtual void link_status(uint32_t vehicle_id, const LinkStatusData&,
                           Stamp) = 0;
};

}  // namespace skyrelay::vehicle
