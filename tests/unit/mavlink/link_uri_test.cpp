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
  for (auto s : {"udp://0.0.0.0", "ftp://127.0.0.1:1", "udp://h:0", "udp://h:70000",
                 "", "udp//h:1", "udp://:5", "udp://h:", "udp://h:1x"}) {
    EXPECT_THROW(parse_link_uri(s), std::invalid_argument) << s;
  }
}

TEST(LinkUri, ParsesTcpAndSerial) {
  auto t = parse_link_uri("tcp://127.0.0.1:5760");
  EXPECT_EQ(t.scheme, LinkScheme::TcpClient);
  EXPECT_EQ(t.host, "127.0.0.1");
  EXPECT_EQ(t.port, 5760);
  auto s = parse_link_uri("serial:///dev/ttyUSB0:57600");
  EXPECT_EQ(s.scheme, LinkScheme::Serial);
  EXPECT_EQ(s.device, "/dev/ttyUSB0");
  EXPECT_EQ(s.baud, 57600u);
  EXPECT_TRUE(s.host.empty());
  EXPECT_EQ(s.port, 0);
}

TEST(LinkUri, RejectsBadTcpSerialAndHostnames) {
  for (auto u : {"serial://dev/ttyUSB0:57600", "serial:///dev/ttyUSB0",
                 "serial:///dev/ttyUSB0:12345", "serial:///dev/ttyUSB0:0",
                 "tcp://localhost:5760", "tcp://127.0.0.1:0",
                 "udp://example.com:14540"}) {
    EXPECT_THROW(parse_link_uri(u), std::invalid_argument) << u;
  }
}

TEST(LinkUri, ToStringRoundTrips) {
  for (auto u : {"udp://0.0.0.0:14540", "udpout://127.0.0.1:14550",
                 "tcp://127.0.0.1:5760", "serial:///dev/ttyACM0:115200"}) {
    EXPECT_EQ(to_string(parse_link_uri(u)), u);
  }
}
