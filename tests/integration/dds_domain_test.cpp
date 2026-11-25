#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "skyrelay/dds/domain.hpp"

namespace {

namespace asio = boost::asio;
using asio::ip::udp;
using namespace std::chrono_literals;

// What the default (loopback) configuration does and does not promise.
//
// Cyclone 11.1 binds its unicast receive sockets to the wildcard address
// whenever only one interface is configured, so the loopback configuration
// does not stop another host from sending to them. What it does control is
// where the participant sends and which addresses it advertises: peers learn
// a participant's locators from its SPDP announcement, so if every locator in
// it is 127.0.0.1, no conforming peer elsewhere is ever told to reach it, and
// it sends nothing off the host. This test checks exactly that, on the wire.

// A domain id no other test uses, so this process owns its port range.
constexpr uint32_t kDomain = 93;

// Cyclone's default port mapping: 7400 + 250 * domain, discovery unicast at
// +10 + 2 * participant index (see loopback_udp_ports).
constexpr uint16_t discovery_port(uint32_t domain, uint32_t index) {
  return static_cast<uint16_t>(7400 + 250 * domain + 10 + 2 * index);
}

struct Locator {
  uint16_t pid;
  int32_t kind;
  uint32_t port;
  std::array<uint8_t, 16> address;
};

constexpr int32_t kLocatorUdpV4 = 1;
constexpr uint16_t kPidSentinel = 0x0001;
constexpr uint16_t kPidDefaultUnicast = 0x0031;
constexpr uint16_t kPidMetatrafficUnicast = 0x0032;
constexpr uint16_t kPidMetatrafficMulticast = 0x0033;
constexpr uint16_t kPidDefaultMulticast = 0x0048;

bool is_locator_pid(uint16_t pid) {
  return pid == kPidDefaultUnicast || pid == kPidMetatrafficUnicast ||
         pid == kPidMetatrafficMulticast || pid == kPidDefaultMulticast;
}

uint16_t u16(std::span<const uint8_t> b, size_t at, bool little) {
  return little ? static_cast<uint16_t>(b[at] | (b[at + 1] << 8))
                : static_cast<uint16_t>((b[at] << 8) | b[at + 1]);
}

uint32_t u32(std::span<const uint8_t> b, size_t at, bool little) {
  uint32_t v = 0;
  for (size_t i = 0; i < 4; ++i) {
    const uint32_t byte = b[at + (little ? 3 - i : i)];
    v = (v << 8) | byte;
  }
  return v;
}

// The locators in the SPDP DATA submessages of one RTPS datagram, or nullopt
// if it holds none. Only what this test needs of RTPS 2.x (section 9.4).
std::optional<std::vector<Locator>> spdp_locators(std::span<const uint8_t> b) {
  if (b.size() < 20 || b[0] != 'R' || b[1] != 'T' || b[2] != 'P' || b[3] != 'S') {
    return std::nullopt;
  }
  std::optional<std::vector<Locator>> out;
  size_t at = 20;
  while (at + 4 <= b.size()) {
    const uint8_t id = b[at];
    const uint8_t flags = b[at + 1];
    const bool little = flags & 0x01;
    const size_t len = u16(b, at + 2, little);
    const size_t body = at + 4;
    const size_t end = len == 0 ? b.size() : body + len;
    if (end > b.size()) break;
    // DATA with serialized data (D flag), written by the SPDP writer.
    constexpr uint8_t kData = 0x15;
    const std::array<uint8_t, 4> spdp_writer{0x00, 0x01, 0x00, 0xc2};
    if (id == kData && (flags & 0x04) && end - body >= 20 &&
        std::equal(spdp_writer.begin(), spdp_writer.end(), b.begin() + body + 8)) {
      size_t p = body + 4 + u16(b, body + 2, little);
      if (flags & 0x02) {  // inline QoS: a parameter list to skip
        while (p + 4 <= end && u16(b, p, little) != kPidSentinel) {
          p += 4 + u16(b, p + 2, little);
        }
        p += 4;
      }
      if (p + 4 > end) break;
      // Encapsulation: PL_CDR_BE (0x0002) or PL_CDR_LE (0x0003).
      const bool pl_little = b[p + 1] == 0x03;
      p += 4;
      std::vector<Locator> locs;
      while (p + 4 <= end) {
        const uint16_t pid = u16(b, p, pl_little);
        const uint16_t plen = u16(b, p + 2, pl_little);
        if (pid == kPidSentinel || p + 4 + plen > end) break;
        if (is_locator_pid(pid) && plen >= 24) {
          Locator l{};
          l.pid = pid;
          l.kind = static_cast<int32_t>(u32(b, p + 4, pl_little));
          l.port = u32(b, p + 8, pl_little);
          std::copy_n(b.begin() + static_cast<std::ptrdiff_t>(p + 12), 16,
                      l.address.begin());
          locs.push_back(l);
        }
        p += 4 + plen;
      }
      if (!out) out.emplace();
      out->insert(out->end(), locs.begin(), locs.end());
    }
    at = end;
  }
  return out;
}

std::string describe(const Locator& l) {
  char pid[8];
  std::snprintf(pid, sizeof pid, "%04x", l.pid);
  std::string s = std::string("pid 0x") + pid + " kind " + std::to_string(l.kind) +
                  " port " + std::to_string(l.port) + " address";
  for (const uint8_t byte : l.address) s += " " + std::to_string(byte);
  return s;
}

}  // namespace

TEST(DdsDomain, DefaultParticipantAdvertisesOnlyLoopback) {
  // The participant pings the discovery port of every participant index up to
  // kMaxAutoParticipantIndex on 127.0.0.1. It takes index 0 itself, so a
  // socket on the last index's port receives its SPDP announcements.
  asio::io_context io;
  udp::socket sock(io, udp::endpoint(
                           asio::ip::make_address("127.0.0.1"),
                           discovery_port(kDomain,
                                          skyrelay::dds::kMaxAutoParticipantIndex)));
  sock.non_blocking(true);

  skyrelay::dds::DomainConfig dc;
  dc.domain_id = kDomain;
  dc.partition = "dds-domain-test";
  auto dp = skyrelay::dds::open_participant(dc);

  std::optional<std::vector<Locator>> locators;
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  std::vector<uint8_t> buf(65536);
  while (!locators && std::chrono::steady_clock::now() < deadline) {
    udp::endpoint from;
    boost::system::error_code ec;
    const size_t n = sock.receive_from(asio::buffer(buf), from, 0, ec);
    if (ec) {
      std::this_thread::sleep_for(10ms);
      continue;
    }
    // Sent from loopback, to loopback.
    EXPECT_TRUE(from.address().is_loopback()) << from.address().to_string();
    locators = spdp_locators(std::span<const uint8_t>(buf.data(), n));
  }
  ASSERT_TRUE(locators) << "no SPDP announcement within 5 s";

  const skyrelay::dds::PortRange ports = skyrelay::dds::loopback_udp_ports(kDomain);
  size_t unicast = 0;
  for (const Locator& l : *locators) {
    EXPECT_GE(l.port, ports.first) << describe(l);
    EXPECT_LE(l.port, ports.last) << describe(l);
    EXPECT_TRUE(l.pid == kPidDefaultUnicast || l.pid == kPidMetatrafficUnicast)
        << "multicast locator advertised: " << describe(l);
    EXPECT_EQ(l.kind, kLocatorUdpV4) << describe(l);
    const std::array<uint8_t, 16> loopback{0, 0, 0, 0, 0, 0, 0, 0,
                                           0, 0, 0, 0, 127, 0, 0, 1};
    EXPECT_EQ(l.address, loopback) << describe(l);
    ++unicast;
  }
  // At least the discovery (metatraffic) and the data (default) locator.
  EXPECT_GE(unicast, 2u);
}
