#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace skyrelay::mav {

// tcp and serial schemes arrive with M2.
enum class LinkScheme { UdpListen, UdpOut };

struct LinkUri {
  LinkScheme scheme;
  std::string host;
  uint16_t port;
};

// Accepts udp://host:port (listen, reply to the last sender) and
// udpout://host:port (send to that endpoint). Port 0 is rejected.
// Throws std::invalid_argument naming the offending part.
LinkUri parse_link_uri(std::string_view uri);

}  // namespace skyrelay::mav
