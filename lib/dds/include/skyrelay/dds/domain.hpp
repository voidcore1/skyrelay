#pragma once

#include <cstdint>
#include <dds/dds.hpp>
#include <string>

namespace skyrelay::dds {

struct DomainConfig {
  uint32_t domain_id = 42;
  std::string partition = "default";
  // INSECURE PREVIEW: off by default. When false, Cyclone is bound to the
  // loopback interface only and discovers peers over unicast to 127.0.0.1.
  bool allow_network = false;
};

// Cyclone configuration for the domain. The loopback marker is the
// <NetworkInterface address="127.0.0.1"> element: Cyclone then uses only that
// interface. Multicast is disabled (loopback has no multicast route on most
// hosts) and discovery uses the unicast peer 127.0.0.1 with automatic
// participant indices, so participants in one process, or in several, find
// each other. With allow_network the interface and peer restrictions are
// dropped and Cyclone's defaults apply.
std::string cyclone_config_xml(const DomainConfig&);

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
