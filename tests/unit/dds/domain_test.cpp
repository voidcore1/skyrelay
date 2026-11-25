#include <gtest/gtest.h>

#include <string>

#include "skyrelay/dds/domain.hpp"

using namespace skyrelay::dds;

// What the loopback configuration actually does on the wire is checked by
// DdsDomain.DefaultParticipantAdvertisesOnlyLoopback in the integration tests.

TEST(Domain, AllowNetworkDropsRestriction) {
  auto x = cyclone_config_xml({.allow_network = true});
  EXPECT_EQ(x.find("127.0.0.1"), std::string::npos);
  EXPECT_EQ(x.find("NetworkInterface"), std::string::npos);
}

TEST(Domain, DomainIdFollowsConfig) {
  auto x = cyclone_config_xml({.domain_id = 7});
  EXPECT_NE(x.find("id=\"7\""), std::string::npos);
  EXPECT_NE(cyclone_config_xml({}).find("id=\"42\""), std::string::npos);
}

TEST(Domain, LoopbackPortRangeFollowsCyclonesMapping) {
  const PortRange r = loopback_udp_ports(42);
  EXPECT_EQ(r.first, 17910u);
  EXPECT_EQ(r.last, 18039u);
  EXPECT_EQ(loopback_udp_ports(0).first, 7410u);
  EXPECT_NE(cyclone_config_xml({}).find("<MaxAutoParticipantIndex>64<"),
            std::string::npos);
}
