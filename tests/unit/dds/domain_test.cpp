#include <gtest/gtest.h>

#include <string>

#include "skyrelay/dds/domain.hpp"

using namespace skyrelay::dds;

// The loopback marker is the NetworkInterface element pinned to 127.0.0.1.
TEST(Domain, LoopbackOnlyByDefault) {
  auto x = cyclone_config_xml({});
  EXPECT_NE(x.find("<NetworkInterface address=\"127.0.0.1\""), std::string::npos);
  EXPECT_NE(x.find("<AllowMulticast>false</AllowMulticast>"), std::string::npos);
  EXPECT_NE(x.find("id=\"42\""), std::string::npos);
}

TEST(Domain, AllowNetworkDropsRestriction) {
  auto x = cyclone_config_xml({.allow_network = true});
  EXPECT_EQ(x.find("127.0.0.1"), std::string::npos);
  EXPECT_EQ(x.find("NetworkInterface"), std::string::npos);
}

TEST(Domain, DomainIdFollowsConfig) {
  auto x = cyclone_config_xml({.domain_id = 7});
  EXPECT_NE(x.find("id=\"7\""), std::string::npos);
}
