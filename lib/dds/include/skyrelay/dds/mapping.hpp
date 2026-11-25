#pragma once

#include <cstdint>

#include "skyrelay/dds/idl.hpp"
#include "skyrelay/vehicle/telemetry_sink.hpp"
#include "skyrelay/vehicle/types.hpp"

namespace skyrelay::dds {

// Domain structs to IDL v1 samples. valid_mask follows one rule: a float
// field's bit is set iff it is not NaN, an optional field's bit iff it has a
// value. Fields the bridge always knows (see the .cpp) are always set.
// Unset non-floats are written as 0, empty or the first enum literal.
v1::VehicleInfo to_idl(uint32_t vid, const vehicle::InfoData&, vehicle::Stamp,
                       uint64_t seq);
v1::VehicleState to_idl(uint32_t vid, const vehicle::StateData&, vehicle::Stamp,
                        uint64_t seq);
v1::Pose to_idl(uint32_t vid, const vehicle::PoseData&, vehicle::Stamp,
                uint64_t seq);
v1::Health to_idl(uint32_t vid, const vehicle::HealthData&, vehicle::Stamp,
                  uint64_t seq);
v1::LinkStatus to_idl(uint32_t vid, const vehicle::LinkStatusData&,
                      vehicle::Stamp, uint64_t seq);

}  // namespace skyrelay::dds
