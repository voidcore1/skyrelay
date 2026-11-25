#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "skyrelay/mav/tcp_link.hpp"

using namespace skyrelay::mav;
using boost::asio::ip::tcp;
using namespace std::chrono_literals;

namespace {

std::vector<uint8_t> bytes_of(const std::string& s) {
  return {s.begin(), s.end()};
}

template <class Done>
bool run_until(boost::asio::io_context& io, Done done,
               std::chrono::milliseconds limit = 2s) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    io.run_for(10ms);
  }
  return done();
}

LinkUri uri_for(const tcp::acceptor& acc) {
  return LinkUri{LinkScheme::TcpClient, "127.0.0.1",
                 acc.local_endpoint().port()};
}

// Accepts one connection, driving the loop until it arrives.
bool accept_one(boost::asio::io_context& io, tcp::acceptor& acc,
                tcp::socket& out) {
  bool done = false;
  acc.async_accept(out, [&](const boost::system::error_code& ec) {
    done = !ec;
  });
  return run_until(io, [&] { return done; });
}

}  // namespace

TEST(TcpLink, ConnectsSendsAndReceives) {
  boost::asio::io_context io;
  tcp::acceptor acc(io, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  std::string got;
  TcpLink link(io, uri_for(acc), [&](std::span<const uint8_t> d) {
    got.append(d.begin(), d.end());
    return true;
  });
  tcp::socket server(io);
  ASSERT_TRUE(accept_one(io, acc, server));
  ASSERT_TRUE(run_until(io, [&] { return link.connected(); }));

  boost::asio::write(server, boost::asio::buffer("ping", 4));
  ASSERT_TRUE(run_until(io, [&] { return got == "ping"; }));

  link.send(bytes_of("pong"));
  std::string read(4, '\0');
  bool done = false;
  boost::asio::async_read(server, boost::asio::buffer(read),
                          [&](const boost::system::error_code& ec, size_t) {
                            done = !ec;
                          });
  ASSERT_TRUE(run_until(io, [&] { return done; }));
  EXPECT_EQ(read, "pong");
  EXPECT_EQ(link.counters().tx_chunks, 1u);
  EXPECT_EQ(link.counters().rx_chunks, 1u);
  EXPECT_EQ(link.counters().reconnects, 0u);
}

TEST(TcpLink, ReconnectsAfterServerRestart) {
  boost::asio::io_context io;
  tcp::acceptor acc(io, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  std::string got;
  TcpLink link(io, uri_for(acc), [&](std::span<const uint8_t> d) {
    got.append(d.begin(), d.end());
    return true;
  });
  tcp::socket server(io);
  ASSERT_TRUE(accept_one(io, acc, server));
  ASSERT_TRUE(run_until(io, [&] { return link.connected(); }));

  server.close();
  ASSERT_TRUE(run_until(io, [&] { return !link.connected(); }));
  tcp::socket server2(io);
  ASSERT_TRUE(accept_one(io, acc, server2));
  ASSERT_TRUE(run_until(io, [&] { return link.connected(); }));
  EXPECT_EQ(link.counters().reconnects, 1u);

  boost::asio::write(server2, boost::asio::buffer("again", 5));
  ASSERT_TRUE(run_until(io, [&] { return got == "again"; }));
  link.send(bytes_of("back"));
  std::string read(4, '\0');
  bool done = false;
  boost::asio::async_read(server2, boost::asio::buffer(read),
                          [&](const boost::system::error_code& ec, size_t) {
                            done = !ec;
                          });
  ASSERT_TRUE(run_until(io, [&] { return done; }));
  EXPECT_EQ(read, "back");
}

TEST(TcpLink, DropsSendWhileDisconnected) {
  boost::asio::io_context io;
  // A port nobody listens on: bind, note the port, close.
  uint16_t port = 0;
  {
    tcp::acceptor acc(io, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
    port = acc.local_endpoint().port();
  }
  TcpLink link(io, LinkUri{LinkScheme::TcpClient, "127.0.0.1", port}, nullptr);
  link.send(bytes_of("x"));
  EXPECT_FALSE(link.connected());
  EXPECT_EQ(link.counters().tx_dropped, 1u);
  EXPECT_EQ(link.counters().tx_chunks, 0u);
}

TEST(TcpLink, DestroyWhileConnectingIsSafe) {
  boost::asio::io_context io;
  tcp::acceptor acc(io, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  int rx_calls = 0;
  {
    TcpLink link(io, uri_for(acc), [&](std::span<const uint8_t>) {
      ++rx_calls;
      return true;
    });
  }
  io.run_for(100ms);
  EXPECT_EQ(rx_calls, 0);
}

TEST(TcpLink, DropsSendsBeyondThePendingLimit) {
  boost::asio::io_context io;
  tcp::acceptor acc(io, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  TcpLink link(io, uri_for(acc), nullptr);
  tcp::socket server(io);
  ASSERT_TRUE(accept_one(io, acc, server));
  ASSERT_TRUE(run_until(io, [&] { return link.connected(); }));

  // The server never reads, so the socket buffers fill and writes queue up.
  const std::vector<uint8_t> chunk(16 * 1024, 0x55);
  for (int i = 0; i < 4000; ++i) {
    link.send(chunk);
    io.poll();
    if (link.counters().tx_dropped > 0) break;
  }
  EXPECT_GT(link.counters().tx_dropped, 0u);
}
