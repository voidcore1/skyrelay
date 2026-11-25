#include "skyrelay/mav/link.hpp"

#include <stdexcept>
#include <utility>

#include "skyrelay/mav/serial_link.hpp"
#include "skyrelay/mav/tcp_link.hpp"
#include "skyrelay/mav/udp_link.hpp"

namespace skyrelay::mav {

std::unique_ptr<Link> open_link(boost::asio::io_context& io, const LinkUri& uri,
                                Link::RxHandler on_rx,
                                LogHandler on_log) {
  switch (uri.scheme) {
    case LinkScheme::UdpListen:
    case LinkScheme::UdpOut:
      return std::make_unique<UdpLink>(io, uri, std::move(on_rx));
    case LinkScheme::TcpClient:
      return std::make_unique<TcpLink>(io, uri, std::move(on_rx),
                                       ReconnectBackoff{}, std::move(on_log));
    case LinkScheme::Serial:
      return std::make_unique<SerialLink>(io, uri, std::move(on_rx),
                                          ReconnectBackoff{},
                                          std::move(on_log));
  }
  throw std::invalid_argument("open_link: unknown link scheme");
}

}  // namespace skyrelay::mav
