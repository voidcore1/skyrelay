#include <gtest/gtest.h>

#include <stdexcept>

#include "skyrelay/mav/link_uri.hpp"

using namespace skyrelay::mav;

TEST(LinkUri, ParsesListenAndOut) {
  auto a = parse_link_uri("udp://0.0.0.0:14540");
  EXPECT_EQ(a.scheme, LinkScheme::UdpListen);
  EXPECT_EQ(a.host, "0.0.0.0");
  EXPECT_EQ(a.port, 14540);
  auto b = parse_link_uri("udpout://127.0.0.1:14550");
  EXPECT_EQ(b.scheme, LinkScheme::UdpOut);
  EXPECT_EQ(b.host, "127.0.0.1");
  EXPECT_EQ(b.port, 14550);
}

TEST(LinkUri, RejectsBadInput) {
  for (auto s : {"udp://0.0.0.0", "tcp://x:1", "udp://h:0", "udp://h:70000",
                 "", "udp//h:1", "udp://:5", "udp://h:", "udp://h:1x"}) {
    EXPECT_THROW(parse_link_uri(s), std::invalid_argument) << s;
  }
}
