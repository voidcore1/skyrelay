#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace skyrelay::mav {

enum class LinkScheme { UdpListen, UdpOut, TcpClient, Serial };

struct LinkUri {
  LinkScheme scheme;
  // An IP literal; empty (and port 0) for Serial.
  std::string host;
  uint16_t port;
  // Serial only: the device path and its baud rate.
  std::string device{};
  uint32_t baud = 0;
};

// The baud rates a serial link accepts.
inline constexpr std::array<uint32_t, 14> kSerialBauds = {
    9600,   19200,  38400,  57600,   115200,  230400,  460800,
    500000, 576000, 921600, 1000000, 1500000, 2000000, 3000000};

// Accepts udp://ip:port (listen, reply to the last accepted sender),
// udpout://ip:port (send to that endpoint), tcp://ip:port (connect to that
// endpoint) and serial:///absolute/path:baud. Hosts must be IP literals, as a
// hostname would need a resolver round trip, and a port must not be 0.
// Throws std::invalid_argument naming the offending part.
LinkUri parse_link_uri(std::string_view uri);

// The inverse of parse_link_uri.
std::string to_string(const LinkUri& uri);

}  // namespace skyrelay::mav
