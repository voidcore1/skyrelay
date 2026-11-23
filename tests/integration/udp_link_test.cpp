#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "skyrelay/mav/udp_link.hpp"

using namespace skyrelay::mav;

namespace {

std::vector<uint8_t> bytes_of(const std::string& s) {
  return {s.begin(), s.end()};
}

std::string text_of(std::span<const uint8_t> b) {
  return {b.begin(), b.end()};
}

// Runs the loop until done() holds or two seconds pass.
template <class Done>
bool run_until(boost::asio::io_context& io, Done done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    io.run_for(std::chrono::milliseconds(10));
  }
  return done();
}

}  // namespace

TEST(UdpLink, ListenRepliesToLastSender) {
  boost::asio::io_context io;
  std::string a_got;
  std::string b_got;
  UdpLink a(io, LinkUri{LinkScheme::UdpListen, "127.0.0.1", 0},
            [&](std::span<const uint8_t> d) { a_got = text_of(d); });
  UdpLink b(io, LinkUri{LinkScheme::UdpOut, "127.0.0.1", a.local_port()},
            [&](std::span<const uint8_t> d) { b_got = text_of(d); });

  b.send(bytes_of("ping"));
  ASSERT_TRUE(run_until(io, [&] { return !a_got.empty(); }));
  EXPECT_EQ(a_got, "ping");

  a.send(bytes_of("pong"));
  ASSERT_TRUE(run_until(io, [&] { return !b_got.empty(); }));
  EXPECT_EQ(b_got, "pong");
  EXPECT_EQ(a.stats().rx_datagrams, 1u);
  EXPECT_EQ(a.stats().tx_datagrams, 1u);
  EXPECT_EQ(b.stats().tx_datagrams, 1u);
}

TEST(UdpLink, ListenDropsSendBeforePeerKnown) {
  boost::asio::io_context io;
  UdpLink a(io, LinkUri{LinkScheme::UdpListen, "127.0.0.1", 0}, nullptr);
  a.send(bytes_of("x"));
  EXPECT_EQ(a.stats().tx_dropped_no_peer, 1u);
  EXPECT_EQ(a.stats().tx_datagrams, 0u);
}
