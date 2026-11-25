#pragma once

#include <cstdint>
#include <dds/dds.hpp>
#include <string>

namespace skyrelay::dds {

struct DomainConfig {
  uint32_t domain_id = 42;
  std::string partition = "default";
  // INSECURE PREVIEW: off by default. When false, Cyclone sends and
  // advertises only on the loopback interface and discovers peers over
  // unicast to 127.0.0.1 (see cyclone_config_xml for what that does not
  // cover).
  bool allow_network = false;
};

// Highest participant index the loopback configuration hands out, so at most
// this many plus one processes per host and domain.
inline constexpr uint32_t kMaxAutoParticipantIndex = 64;

// Cyclone configuration for the domain. Without allow_network it names the
// loopback interface, <NetworkInterface address="127.0.0.1">, so Cyclone sends
// only there and advertises only 127.0.0.1 locators; multicast is disabled
// (loopback has no multicast route on most hosts) and discovery uses the
// unicast peer 127.0.0.1 with automatic participant indices, so participants
// in one process, or in several, find each other.
//
// It does not keep other hosts out. With a single interface configured,
// Cyclone 11.1 binds its unicast receive sockets to the wildcard address
// (0.0.0.0), and has no option to bind them to the interface instead, so
// anything that can reach the ports in loopback_udp_ports() can send to the
// participant. Until DDS-Security arrives that needs a host firewall or a
// trusted network.
//
// With allow_network the interface and peer restrictions are dropped and
// Cyclone's defaults apply.
std::string cyclone_config_xml(const DomainConfig&);

// The UDP ports a participant in the loopback configuration may bind for this
// domain: Cyclone's default mapping, 7400 + 250 * domain + 10 + 2 * index for
// discovery and one more for data, for indices 0 to kMaxAutoParticipantIndex.
// 17910 to 18039 for domain 42.
struct PortRange {
  uint32_t first;
  uint32_t last;
};
PortRange loopback_udp_ports(uint32_t domain_id);

// One line for the startup log: how far DDS reaches with this configuration.
std::string exposure_summary(const DomainConfig&);

// Creates the Cyclone domain from the XML above, then a participant in it.
// Calling this again for a domain that already exists in the process reuses
// that domain (and its first configuration).
::dds::domain::DomainParticipant open_participant(const DomainConfig&);

// Publisher and subscriber in the configured data partition.
::dds::pub::Publisher make_publisher(::dds::domain::DomainParticipant&,
                                     const DomainConfig&);
::dds::sub::Subscriber make_subscriber(::dds::domain::DomainParticipant&,
                                       const DomainConfig&);

}  // namespace skyrelay::dds
