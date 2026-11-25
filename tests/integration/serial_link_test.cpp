#include <gtest/gtest.h>
#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <vector>

#include "skyrelay/mav/serial_link.hpp"

using namespace skyrelay::mav;
using namespace std::chrono_literals;

namespace {

template <class Done>
bool run_until(boost::asio::io_context& io, Done done,
               std::chrono::milliseconds limit = 2s) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    io.run_for(10ms);
  }
  return done();
}

// A pseudo-terminal pair. The slave side is what the link opens.
struct Pty {
  Pty() {
    int slave = -1;
    char name[256]{};
    if (openpty(&master, &slave, name, nullptr, nullptr) != 0) {
      master = -1;
      return;
    }
    path = name;
    // Raw, so the line discipline leaves the bytes alone.
    termios t{};
    tcgetattr(slave, &t);
    cfmakeraw(&t);
    tcsetattr(slave, TCSANOW, &t);
    ::close(slave);  // the device node stays while the master is open
    // A write that cannot complete must fail the test, not hang it.
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
  }
  ~Pty() { close_master(); }
  void close_master() {
    if (master >= 0) ::close(master);
    master = -1;
  }
  int master = -1;
  std::string path;
};

}  // namespace

TEST(SerialLink, ExchangesBytesOverPty) {
  Pty pty;
  ASSERT_GE(pty.master, 0);
  boost::asio::io_context io;
  std::string got;
  SerialLink link(io, LinkUri{LinkScheme::Serial, "", 0, pty.path, 57600},
                  [&](std::span<const uint8_t> d) {
                    got.append(d.begin(), d.end());
                    return true;
                  });
  ASSERT_TRUE(run_until(io, [&] { return link.open(); }));

  // The master is non-blocking and the pty buffer is empty, so this lands
  // in one go; a short write is a failure.
  ASSERT_EQ(::write(pty.master, "ping", 4), 4);
  ASSERT_TRUE(run_until(io, [&] { return got == "ping"; }));

  const std::vector<uint8_t> pong{'p', 'o', 'n', 'g'};
  link.send(pong);
  std::string read;
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (read.size() < 4 && std::chrono::steady_clock::now() < deadline) {
    io.run_for(10ms);
    char buf[16];
    const ssize_t n = ::read(pty.master, buf, sizeof buf);
    if (n > 0) read.append(buf, static_cast<size_t>(n));
  }
  EXPECT_EQ(read, "pong");
  EXPECT_EQ(link.counters().tx_chunks, 1u);
  EXPECT_EQ(link.counters().rx_chunks, 1u);
}

TEST(SerialLink, BacksOffWhenDeviceGoes) {
  Pty pty;
  ASSERT_GE(pty.master, 0);
  boost::asio::io_context io;
  SerialLink link(io, LinkUri{LinkScheme::Serial, "", 0, pty.path, 57600},
                  nullptr);
  ASSERT_TRUE(run_until(io, [&] { return link.open(); }));

  pty.close_master();
  ASSERT_TRUE(run_until(io, [&] { return link.counters().rx_errors >= 1; }));
  const uint64_t errors_at_loss = link.counters().rx_errors;
  io.run_for(2s);
  // 500 ms then 1 s then 2 s: no spinning on a dead device.
  EXPECT_LE(link.counters().rx_errors - errors_at_loss, 3u);
}
