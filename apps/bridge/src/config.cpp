#include "config.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <map>
#include <sstream>
#include <utility>

namespace skyrelay::bridge {

namespace {

[[noreturn]] void fail(const std::string& key, const std::string& what) {
  throw ConfigError(key + ": " + what);
}

std::string join(const std::string& parent, const std::string& key) {
  return parent.empty() ? key : parent + "." + key;
}

void check_keys(const YAML::Node& map, const std::string& path,
                std::initializer_list<std::string_view> allowed) {
  for (const auto& kv : map) {
    const auto key = kv.first.as<std::string>();
    if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
      fail(join(path, key), "unknown key");
    }
  }
}

// The mapping under `key`, or an undefined node if the key is absent.
YAML::Node section(const YAML::Node& parent, const char* key) {
  const YAML::Node n = parent[key];
  if (n && !n.IsMap()) fail(key, "must be a mapping");
  return n;
}

int64_t integer(const YAML::Node& n, const std::string& path, int64_t lo,
                int64_t hi) {
  int64_t v = 0;
  if (!n.IsScalar() || !YAML::convert<int64_t>::decode(n, v)) {
    fail(path, "must be an integer");
  }
  if (v < lo || v > hi) {
    fail(path, "must be between " + std::to_string(lo) + " and " +
                   std::to_string(hi) + ", got " + std::to_string(v));
  }
  return v;
}

double positive(const YAML::Node& n, const std::string& path, double hi) {
  double v = 0;
  if (!n.IsScalar() || !YAML::convert<double>::decode(n, v) ||
      !std::isfinite(v)) {
    fail(path, "must be a number");
  }
  if (v <= 0 || v > hi) {
    std::ostringstream s;
    s << "must be above 0 and at most " << hi << ", got " << v;
    fail(path, s.str());
  }
  return v;
}

std::string text(const YAML::Node& n, const std::string& path) {
  if (!n.IsScalar() || n.Scalar().empty()) fail(path, "must be a non-empty string");
  return n.Scalar();
}

void read_dds(const YAML::Node& n, DdsSettings& d) {
  if (!n) return;
  check_keys(n, "dds", {"domain_id", "partition"});
  // Cyclone's default port mapping has room for domains 0 to 232.
  if (n["domain_id"]) {
    d.domain_id = static_cast<uint32_t>(
        integer(n["domain_id"], "dds.domain_id", 0, 232));
  }
  if (n["partition"]) d.partition = text(n["partition"], "dds.partition");
}

void read_link(const YAML::Node& n, mav::LinkThresholds& t) {
  if (!n) return;
  check_keys(n, "link",
             {"degraded_gap_ms", "lost_gap_ms", "degraded_loss_pct",
              "recover_healthy_ms", "recovering_lost_gap_ms",
              "min_loss_samples"});
  const auto ms = [&](const char* key, std::chrono::milliseconds& out,
                      int64_t lo) {
    if (n[key]) {
      out = std::chrono::milliseconds{
          integer(n[key], join("link", key), lo, 600'000)};
    }
  };
  ms("degraded_gap_ms", t.degraded_gap, 1);
  ms("lost_gap_ms", t.lost_gap, 1);
  ms("recover_healthy_ms", t.recover_healthy, 0);
  ms("recovering_lost_gap_ms", t.recovering_lost_gap, 1);
  if (n["degraded_loss_pct"]) {
    t.degraded_loss_pct =
        positive(n["degraded_loss_pct"], "link.degraded_loss_pct", 100);
  }
  if (n["min_loss_samples"]) {
    t.min_loss_samples = static_cast<size_t>(
        integer(n["min_loss_samples"], "link.min_loss_samples", 1, 100'000));
  }
  if (t.lost_gap <= t.degraded_gap) {
    fail("link.lost_gap_ms", "must be greater than link.degraded_gap_ms");
  }
  // A recovering link only has the 1 Hz heartbeat until its setup is done, so
  // it must not be given up sooner than a connected one.
  if (t.recovering_lost_gap < t.lost_gap) {
    fail("link.recovering_lost_gap_ms",
         "must be at least link.lost_gap_ms (" +
             std::to_string(t.lost_gap.count()) + "), got " +
             std::to_string(t.recovering_lost_gap.count()));
  }
}

void read_rates(const YAML::Node& n, vehicle::Rates& r) {
  if (!n) return;
  check_keys(n, "rates", {"pose_hz", "state_hz", "health_hz"});
  if (n["pose_hz"]) r.pose_hz = positive(n["pose_hz"], "rates.pose_hz", 1000);
  if (n["state_hz"]) {
    r.state_hz = positive(n["state_hz"], "rates.state_hz", 1000);
  }
  if (n["health_hz"]) {
    r.health_hz = positive(n["health_hz"], "rates.health_hz", 1000);
  }
}

std::vector<VehicleConfig> read_vehicles(const YAML::Node& n,
                                         uint8_t gcs_sysid) {
  if (!n) fail("vehicles", "is required");
  if (!n.IsSequence() || n.size() == 0) {
    fail("vehicles", "must be a non-empty list");
  }
  if (n.size() > kMaxVehicles) {
    fail("vehicles", "at most " + std::to_string(kMaxVehicles) +
                         " vehicles per process (one MAVLink channel each), "
                         "got " +
                         std::to_string(n.size()));
  }

  std::vector<VehicleConfig> out;
  std::map<uint32_t, size_t> ids;
  // A listen link owns its local port; an out link is unique per peer.
  std::map<uint16_t, size_t> listen_ports;
  std::map<std::pair<std::string, uint16_t>, size_t> peers;
  std::map<std::pair<std::string, uint16_t>, size_t> tcp_peers;
  std::map<std::string, size_t> serial_devices;
  for (size_t i = 0; i < n.size(); ++i) {
    const YAML::Node v = n[i];
    const std::string path = "vehicles[" + std::to_string(i) + "]";
    if (!v.IsMap()) fail(path, "must be a mapping");
    check_keys(v, path, {"id", "name", "link", "sysid"});

    VehicleConfig vc;
    if (!v["id"]) fail(path + ".id", "is required");
    vc.id = static_cast<uint32_t>(
        integer(v["id"], path + ".id", 1, UINT32_MAX));
    if (const auto [it, fresh] = ids.emplace(vc.id, i); !fresh) {
      fail(path + ".id", "duplicate id " + std::to_string(vc.id) +
                             " (also vehicles[" + std::to_string(it->second) +
                             "])");
    }
    vc.name = v["name"] ? text(v["name"], path + ".name")
                        : "vehicle-" + std::to_string(vc.id);
    if (v["sysid"]) {
      vc.sysid = static_cast<uint8_t>(integer(v["sysid"], path + ".sysid", 1, 255));
      if (*vc.sysid == gcs_sysid) {
        fail(path + ".sysid", "must differ from gcs_sysid (" +
                                  std::to_string(gcs_sysid) + ")");
      }
    }

    if (!v["link"]) fail(path + ".link", "is required");
    const std::string uri = text(v["link"], path + ".link");
    try {
      vc.link = mav::parse_link_uri(uri);
    } catch (const std::invalid_argument& e) {
      fail(path + ".link", e.what());
    }
    switch (vc.link.scheme) {
      case mav::LinkScheme::UdpListen:
        if (const auto [it, fresh] = listen_ports.emplace(vc.link.port, i);
            !fresh) {
          fail(path + ".link",
               "local port " + std::to_string(vc.link.port) +
                   " is already used by vehicles[" +
                   std::to_string(it->second) + "]");
        }
        break;
      case mav::LinkScheme::UdpOut:
      case mav::LinkScheme::TcpClient: {
        auto& seen =
            vc.link.scheme == mav::LinkScheme::UdpOut ? peers : tcp_peers;
        if (const auto [it, fresh] =
                seen.emplace(std::pair{vc.link.host, vc.link.port}, i);
            !fresh) {
          fail(path + ".link",
               "remote " + vc.link.host + ":" + std::to_string(vc.link.port) +
                   " is already used by vehicles[" +
                   std::to_string(it->second) + "]");
        }
        break;
      }
      case mav::LinkScheme::Serial:
        if (const auto [it, fresh] = serial_devices.emplace(vc.link.device, i);
            !fresh) {
          fail(path + ".link",
               "serial device " + vc.link.device +
                   " already used by vehicles[" + std::to_string(it->second) +
                   "]");
        }
        break;
    }
    out.push_back(std::move(vc));
  }
  return out;
}

}  // namespace

BridgeConfig parse_config(std::string_view yaml) {
  BridgeConfig c;
  try {
    const YAML::Node root = YAML::Load(std::string(yaml));
    if (!root.IsMap()) fail("config", "must be a mapping with a vehicles list");
    check_keys(root, "", {"dds", "gcs_sysid", "link", "rates", "vehicles"});
    read_dds(section(root, "dds"), c.dds);
    if (root["gcs_sysid"]) {
      c.gcs_sysid =
          static_cast<uint8_t>(integer(root["gcs_sysid"], "gcs_sysid", 1, 255));
    }
    read_link(section(root, "link"), c.link);
    read_rates(section(root, "rates"), c.rates);
    c.vehicles = read_vehicles(root["vehicles"], c.gcs_sysid);
  } catch (const YAML::Exception& e) {
    throw ConfigError(std::string("config: ") + e.what());
  }
  return c;
}

BridgeConfig load_config(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw ConfigError("file cannot be read");
  std::ostringstream s;
  s << in.rdbuf();
  return parse_config(s.str());
}

}  // namespace skyrelay::bridge
