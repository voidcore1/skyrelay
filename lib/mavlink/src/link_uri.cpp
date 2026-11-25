#include "skyrelay/mav/link_uri.hpp"

#include <stdexcept>

namespace skyrelay::mav {

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
  if (port_text.empty() || port_text.size() > 5) {
    throw std::invalid_argument("link uri '" + std::string(uri) +
                                "': bad port '" + std::string(port_text) + "'");
  }
  unsigned port = 0;
  for (const char c : port_text) {
    if (c < '0' || c > '9') {
      throw std::invalid_argument("link uri '" + std::string(uri) +
                                  "': bad port '" + std::string(port_text) +
                                  "'");
    }
    port = port * 10 + static_cast<unsigned>(c - '0');
  }
  if (port == 0 || port > 65535) {
    throw std::invalid_argument("link uri '" + std::string(uri) +
                                "': port out of range 1-65535");
  }
  return LinkUri{parsed_scheme, std::string(host), static_cast<uint16_t>(port)};
}

}  // namespace skyrelay::mav
