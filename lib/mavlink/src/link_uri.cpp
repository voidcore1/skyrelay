#include "skyrelay/mav/link_uri.hpp"

#include <algorithm>
#include <stdexcept>

#include <boost/asio/ip/address.hpp>

namespace skyrelay::mav {

namespace {

[[noreturn]] void bad(std::string_view uri, const std::string& what) {
  throw std::invalid_argument("link uri '" + std::string(uri) + "': " + what);
}

// Decimal digits only; port and baud both go through here.
unsigned long number(std::string_view uri, std::string_view text,
                     const char* what) {
  if (text.empty() || text.size() > 9) {
    bad(uri, std::string("bad ") + what + " '" + std::string(text) + "'");
  }
  unsigned long v = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      bad(uri, std::string("bad ") + what + " '" + std::string(text) + "'");
    }
    v = v * 10 + static_cast<unsigned long>(c - '0');
  }
  return v;
}

LinkUri parse_serial(std::string_view uri, std::string_view rest) {
  if (rest.empty() || rest.front() != '/') {
    bad(uri, "serial device must be an absolute path (serial:///dev/...:baud)");
  }
  const auto colon = rest.rfind(':');
  if (colon == std::string_view::npos) bad(uri, "missing baud rate");
  const std::string_view device = rest.substr(0, colon);
  if (device.size() < 2) bad(uri, "empty serial device");
  const unsigned long baud = number(uri, rest.substr(colon + 1), "baud rate");
  if (std::find(kSerialBauds.begin(), kSerialBauds.end(), baud) ==
      kSerialBauds.end()) {
    bad(uri, "unsupported baud rate " + std::to_string(baud));
  }
  return LinkUri{LinkScheme::Serial, "", 0, std::string(device),
                 static_cast<uint32_t>(baud)};
}

}  // namespace

LinkUri parse_link_uri(std::string_view uri) {
  const auto sep = uri.find("://");
  if (sep == std::string_view::npos) {
    throw std::invalid_argument("link uri '" + std::string(uri) +
                                "': missing scheme separator '://'");
  }
  const std::string_view scheme = uri.substr(0, sep);
  LinkScheme parsed_scheme{};
  if (scheme == "udp") {
    parsed_scheme = LinkScheme::UdpListen;
  } else if (scheme == "udpout") {
    parsed_scheme = LinkScheme::UdpOut;
  } else if (scheme == "tcp") {
    parsed_scheme = LinkScheme::TcpClient;
  } else if (scheme == "serial") {
    return parse_serial(uri, uri.substr(sep + 3));
  } else {
    throw std::invalid_argument("link uri '" + std::string(uri) +
                                "': unsupported scheme '" +
                                std::string(scheme) + "'");
  }

  const std::string_view rest = uri.substr(sep + 3);
  const auto colon = rest.rfind(':');
  if (colon == std::string_view::npos) {
    throw std::invalid_argument("link uri '" + std::string(uri) +
                                "': missing port");
  }
  const std::string_view host = rest.substr(0, colon);
  const std::string_view port_text = rest.substr(colon + 1);
  if (host.empty()) {
    throw std::invalid_argument("link uri '" + std::string(uri) +
                                "': empty host");
  }
  const unsigned long port = number(uri, port_text, "port");
  if (port == 0 || port > 65535) {
    throw std::invalid_argument("link uri '" + std::string(uri) +
                                "': port out of range 1-65535");
  }
  boost::system::error_code ec;
  boost::asio::ip::make_address(std::string(host), ec);
  if (ec) {
    bad(uri, "host '" + std::string(host) +
                 "' is not an IP address (hostnames are not resolved)");
  }
  return LinkUri{parsed_scheme, std::string(host), static_cast<uint16_t>(port)};
}

std::string to_string(const LinkUri& uri) {
  switch (uri.scheme) {
    case LinkScheme::UdpListen:
      return "udp://" + uri.host + ":" + std::to_string(uri.port);
    case LinkScheme::UdpOut:
      return "udpout://" + uri.host + ":" + std::to_string(uri.port);
    case LinkScheme::TcpClient:
      return "tcp://" + uri.host + ":" + std::to_string(uri.port);
    case LinkScheme::Serial:
      return "serial://" + uri.device + ":" + std::to_string(uri.baud);
  }
  return "?";
}

}  // namespace skyrelay::mav
