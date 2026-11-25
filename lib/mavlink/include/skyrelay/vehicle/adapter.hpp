#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "skyrelay/mav/mavlink.hpp"
#include "skyrelay/mav/setup_sequence.hpp"
#include "skyrelay/vehicle/types.hpp"

namespace skyrelay::vehicle {

// The one place per-autopilot differences live. The vehicle model is generic
// MAVLink; anything that only one firmware does goes behind this interface.
class AutopilotAdapter {
 public:
  virtual ~AutopilotAdapter() = default;

  virtual Autopilot kind() const = 0;

  // Turns the HEARTBEAT base_mode / custom_mode pair into a mode. A mode that
  // cannot be decoded is FlightMode::Unknown, with raw saying what it was.
  virtual ModeInfo decode_mode(uint8_t base_mode, uint32_t custom_mode) const = 0;

  // Messages to request when the vehicle connects. The default is the set the
  // bridge publishes from.
  virtual std::vector<mav::IntervalRequest> telemetry_intervals() const;
};

class Px4Adapter final : public AutopilotAdapter {
 public:
  Autopilot kind() const override { return Autopilot::Px4; }
  ModeInfo decode_mode(uint8_t base_mode, uint32_t custom_mode) const override;
};

// For anything that is not PX4. It cannot decode custom modes, so the mode is
// Unknown and raw carries the number.
class GenericMavlinkAdapter final : public AutopilotAdapter {
 public:
  explicit GenericMavlinkAdapter(Autopilot kind = Autopilot::Other)
      : kind_(kind) {}
  Autopilot kind() const override { return kind_; }
  ModeInfo decode_mode(uint8_t base_mode, uint32_t custom_mode) const override;

 private:
  Autopilot kind_;
};

// mav_autopilot is the MAV_AUTOPILOT value from the HEARTBEAT. 12 (PX4) gets
// the PX4 adapter, everything else the generic one.
std::unique_ptr<AutopilotAdapter> make_adapter(uint8_t mav_autopilot);

}  // namespace skyrelay::vehicle
