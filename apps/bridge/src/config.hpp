#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "skyrelay/mav/link_monitor.hpp"
#include "skyrelay/mav/link_uri.hpp"
#include "skyrelay/vehicle/session.hpp"

namespace skyrelay::bridge {

// A configuration that cannot be used. The message starts with the key it is
// about, e.g. "vehicles[1].link: ...".
class ConfigError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct DdsSettings {
  uint32_t domain_id = 42;
  std::string partition = "default";
};

struct VehicleConfig {
  uint32_t id = 0;
  std::string name;
  mav::LinkUri link{};
  // The vehicle's MAVLink system id. Unset: the session locks to the first
  // autopilot HEARTBEAT on the link.
  std::optional<uint8_t> sysid;
};

struct BridgeConfig {
  DdsSettings dds;
  uint8_t gcs_sysid = 245;
  mav::LinkThresholds link{};
  vehicle::Rates rates{};
  // In file order; a vehicle's index here is its MAVLink channel.
  std::vector<VehicleConfig> vehicles;
};

// One process runs at most this many vehicles: one MAVLink channel each.
inline constexpr size_t kMaxVehicles = 16;

// The YAML layout, every key but `vehicles` optional:
//
//   dds: { domain_id: 42, partition: default }
//   gcs_sysid: 245
//   link: { degraded_gap_ms: 250, lost_gap_ms: 1000, degraded_loss_pct: 20,
//           recover_healthy_ms: 500, recovering_lost_gap_ms: 2500,
//           min_loss_samples: 10 }
//   rates: { pose_hz: 20, state_hz: 5, health_hz: 2 }
//   vehicles: [ { id: 1, name: px4-0, link: "udp://0.0.0.0:14540",
//                 sysid: 1 }, ... ]
//
// A vehicle's sysid (1-255, not gcs_sysid) is optional. Unknown keys, values
// out of range, recovering_lost_gap_ms below lost_gap_ms, duplicate vehicle
// ids, listen ports or out peers, and more than kMaxVehicles vehicles throw
// ConfigError.
BridgeConfig parse_config(std::string_view yaml);
// As parse_config; an unreadable file is a ConfigError too. Messages do not
// repeat the path.
BridgeConfig load_config(const std::string& path);

}  // namespace skyrelay::bridge
