#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>

#include "config.hpp"

using namespace skyrelay;
using namespace skyrelay::bridge;
using namespace std::chrono_literals;

namespace {

// The error message of parsing `yaml`, or "" if it parsed.
std::string error_of(const std::string& yaml) {
  try {
    parse_config(yaml);
  } catch (const ConfigError& e) {
    return e.what();
  }
  return "";
}

std::string vehicles_yaml(int n) {
  std::string y = "vehicles:\n";
  for (int i = 0; i < n; ++i) {
    y += "  - { id: " + std::to_string(i + 1) +
         ", link: \"udp://0.0.0.0:" + std::to_string(14540 + i) + "\" }\n";
  }
  return y;
}

}  // namespace

TEST(Config, ParsesExample) {
  const BridgeConfig c =
      load_config(SKYRELAY_SOURCE_DIR "/config/bridge.sih-px4-4.yaml");

  EXPECT_EQ(c.dds.domain_id, 42u);
  EXPECT_EQ(c.dds.partition, "default");
  EXPECT_EQ(c.gcs_sysid, 245);
  EXPECT_EQ(c.link.degraded_gap, 250ms);
  EXPECT_EQ(c.link.lost_gap, 1000ms);
  EXPECT_DOUBLE_EQ(c.link.degraded_loss_pct, 20.0);
  EXPECT_EQ(c.link.recover_healthy, 500ms);
  EXPECT_DOUBLE_EQ(c.rates.pose_hz, 20.0);
  EXPECT_DOUBLE_EQ(c.rates.state_hz, 5.0);
  EXPECT_DOUBLE_EQ(c.rates.health_hz, 2.0);

  ASSERT_EQ(c.vehicles.size(), 4u);
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(c.vehicles[i].id, i + 1);
    EXPECT_EQ(c.vehicles[i].name, "px4-" + std::to_string(i));
    EXPECT_EQ(c.vehicles[i].link.scheme, mav::LinkScheme::UdpListen);
    EXPECT_EQ(c.vehicles[i].link.port, 14540 + i);
    EXPECT_EQ(c.vehicles[i].sysid, static_cast<uint8_t>(i + 1));
  }
}

TEST(Config, RejectsDuplicatePortAndUnknownKey) {
  const std::string dup_port = error_of(
      "vehicles:\n"
      "  - { id: 1, link: \"udp://0.0.0.0:14540\" }\n"
      "  - { id: 2, link: \"udp://0.0.0.0:14540\" }\n");
  EXPECT_NE(dup_port.find("vehicles[1].link"), std::string::npos) << dup_port;
  EXPECT_NE(dup_port.find("local port 14540"), std::string::npos) << dup_port;

  const std::string unknown =
      error_of("rats: { pose_hz: 20 }\n" + vehicles_yaml(1));
  EXPECT_NE(unknown.find("rats"), std::string::npos) << unknown;
}

TEST(Config, DuplicateOutPeerNamesTheRemote) {
  const std::string e = error_of(
      "vehicles:\n"
      "  - { id: 1, link: \"udpout://10.0.0.5:14550\" }\n"
      "  - { id: 2, link: \"udpout://10.0.0.5:14550\" }\n");
  EXPECT_NE(e.find("vehicles[1].link"), std::string::npos) << e;
  EXPECT_NE(e.find("remote 10.0.0.5:14550"), std::string::npos) << e;
  EXPECT_EQ(e.find("local port"), std::string::npos) << e;
  // Same port on another host is a different peer.
  EXPECT_EQ(error_of("vehicles:\n"
                     "  - { id: 1, link: \"udpout://10.0.0.5:14550\" }\n"
                     "  - { id: 2, link: \"udpout://10.0.0.6:14550\" }\n"),
            "");
}

TEST(Config, VehicleSysid) {
  EXPECT_FALSE(parse_config(vehicles_yaml(1)).vehicles[0].sysid.has_value());
  const BridgeConfig c = parse_config(
      "vehicles:\n  - { id: 1, link: \"udp://0.0.0.0:14540\", sysid: 42 }\n");
  EXPECT_EQ(c.vehicles[0].sysid, std::optional<uint8_t>(42));

  for (const char* bad : {"0", "256", "-1", "x"}) {
    const std::string e = error_of(
        std::string("vehicles:\n  - { id: 1, link: \"udp://0.0.0.0:14540\", sysid: ") +
        bad + " }\n");
    EXPECT_NE(e.find("vehicles[0].sysid"), std::string::npos) << bad << ": " << e;
  }
  const std::string gcs = error_of(
      "gcs_sysid: 200\nvehicles:\n"
      "  - { id: 1, link: \"udp://0.0.0.0:14540\", sysid: 200 }\n");
  EXPECT_NE(gcs.find("vehicles[0].sysid"), std::string::npos) << gcs;
  EXPECT_NE(gcs.find("gcs_sysid"), std::string::npos) << gcs;
}

TEST(Config, RecoveringLostGapNotBelowLostGap) {
  const std::string e = error_of(
      "link: { lost_gap_ms: 3000, recovering_lost_gap_ms: 2000 }\n" + vehicles_yaml(1));
  EXPECT_NE(e.find("link.recovering_lost_gap_ms"), std::string::npos) << e;
  // The default of 2500 ms is checked too.
  const std::string d = error_of("link: { lost_gap_ms: 3000 }\n" + vehicles_yaml(1));
  EXPECT_NE(d.find("link.recovering_lost_gap_ms"), std::string::npos) << d;
  // Equal is allowed.
  EXPECT_EQ(error_of("link: { lost_gap_ms: 3000, recovering_lost_gap_ms: 3000 }\n" +
                     vehicles_yaml(1)),
            "");
}

TEST(Config, RejectsDuplicateId) {
  const std::string e = error_of(
      "vehicles:\n"
      "  - { id: 3, link: \"udp://0.0.0.0:14540\" }\n"
      "  - { id: 3, link: \"udp://0.0.0.0:14541\" }\n");
  EXPECT_NE(e.find("vehicles[1].id"), std::string::npos) << e;
}

TEST(Config, AcceptsSixteenVehiclesButNotSeventeen) {
  EXPECT_EQ(parse_config(vehicles_yaml(16)).vehicles.size(), 16u);
  const std::string e = error_of(vehicles_yaml(17));
  EXPECT_NE(e.find("vehicles"), std::string::npos) << e;
  EXPECT_NE(e.find("16"), std::string::npos) << e;
}

TEST(Config, OptionalLinkKeysAndDefaults) {
  const BridgeConfig d = parse_config(vehicles_yaml(1));
  EXPECT_EQ(d.gcs_sysid, 245);
  EXPECT_EQ(d.dds.domain_id, 42u);
  EXPECT_EQ(d.link.recovering_lost_gap, 2500ms);
  EXPECT_EQ(d.link.min_loss_samples, 10u);

  const BridgeConfig c = parse_config(
      "link: { recovering_lost_gap_ms: 4000, min_loss_samples: 25 }\n" +
      vehicles_yaml(1));
  EXPECT_EQ(c.link.recovering_lost_gap, 4000ms);
  EXPECT_EQ(c.link.min_loss_samples, 25u);
}

TEST(Config, RejectsBadValuesNamingTheKey) {
  EXPECT_NE(error_of("gcs_sysid: 0\n" + vehicles_yaml(1)).find("gcs_sysid"),
            std::string::npos);
  EXPECT_NE(error_of("rates: { pose_hz: 0 }\n" + vehicles_yaml(1))
                .find("rates.pose_hz"),
            std::string::npos);
  EXPECT_NE(error_of("link: { lost_gap_ms: abc }\n" + vehicles_yaml(1))
                .find("link.lost_gap_ms"),
            std::string::npos);
  EXPECT_NE(error_of("vehicles:\n  - { id: 1, link: \"tcp://1.2.3.4:5\" }\n")
                .find("vehicles[0].link"),
            std::string::npos);
  EXPECT_NE(error_of("vehicles: []\n").find("vehicles"), std::string::npos);
}
