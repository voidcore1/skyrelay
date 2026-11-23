#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

#include <boost/asio.hpp>

#include "config.hpp"
#include "session_runner.hpp"
#include "support/sim_vehicle.hpp"

using namespace skyrelay;
using namespace std::chrono_literals;
namespace asio = boost::asio;
using asio::ip::udp;
using Steady = std::chrono::steady_clock;

namespace {

// Counts calls from the runner's thread; the test thread only reads.
class CountingSink final : public vehicle::TelemetrySink {
 public:
  void vehicle_info(uint32_t, const vehicle::InfoData&,
                    vehicle::Stamp) override {
    ++infos;
  }
  void vehicle_state(uint32_t, const vehicle::StateData&,
                     vehicle::Stamp) override {
    ++states;
  }
  void pose(uint32_t, const vehicle::PoseData&, vehicle::Stamp) override {
    ++poses;
  }
  void health(uint32_t, const vehicle::HealthData&, vehicle::Stamp) override {}
  void link_status(uint32_t, const vehicle::LinkStatusData& d,
                   vehicle::Stamp) override {
    ++links;
    if (d.state == mav::LinkState::Connected) connected = true;
  }

  std::atomic<int> infos{0};
  std::atomic<int> states{0};
  std::atomic<int> poses{0};
  std::atomic<int> links{0};
  std::atomic<bool> connected{false};
};

bridge::VehicleConfig loopback_vehicle() {
  bridge::VehicleConfig v;
  v.id = 3;
  v.name = "test";
  // Port 0: the runner reports what it bound.
  v.link = mav::LinkUri{mav::LinkScheme::UdpListen, "127.0.0.1", 0};
  return v;
}

vehicle::SessionConfig session_config() {
  vehicle::SessionConfig c{};
  c.vehicle_id = 3;
  return c;
}

}  // namespace

TEST(SessionRunner, ServesAVehicleOverUdpAndStopsWithinOneSecond) {
  CountingSink sink;
  bridge::SessionRunner runner(loopback_vehicle(), session_config(), 0, sink);
  runner.start();

  asio::io_context io;
  udp::socket sock(io, udp::endpoint(udp::v4(), 0));
  sock.non_blocking(true);
  const udp::endpoint bridge_ep(asio::ip::make_address("127.0.0.1"),
                                runner.local_port());
  test::SimVehicle sim(1, Steady::now());
  test::FrameParser parser;
  bool gcs_heartbeat = false;

  const auto deadline = Steady::now() + 5s;
  while (Steady::now() < deadline &&
         !(gcs_heartbeat && sink.connected && sink.poses > 0)) {
    const auto out = sim.step(Steady::now());
    if (!out.empty()) sock.send_to(asio::buffer(out), bridge_ep);
    std::array<uint8_t, 2048> buf{};
    udp::endpoint from;
    boost::system::error_code ec;
    for (;;) {
      const size_t n = sock.receive_from(asio::buffer(buf), from, 0, ec);
      if (ec) break;
      const std::span<const uint8_t> got(buf.data(), n);
      for (const auto& m : parser.push(got)) {
        if (m.msgid == MAVLINK_MSG_ID_HEARTBEAT && m.sysid == 245) {
          gcs_heartbeat = true;
        }
      }
      const auto reply = sim.receive(got);
      if (!reply.empty()) sock.send_to(asio::buffer(reply), bridge_ep);
    }
    std::this_thread::sleep_for(5ms);
  }

  EXPECT_TRUE(gcs_heartbeat);
  EXPECT_TRUE(sink.connected);
  EXPECT_GT(sink.poses, 0);
  EXPECT_GT(sink.states, 0);
  EXPECT_EQ(sink.infos, 1);

  const auto t = Steady::now();
  runner.stop();
  EXPECT_LT(Steady::now() - t, 1s);
}

TEST(SessionRunner, TicksWithoutTrafficAndStopsTwice) {
  CountingSink sink;
  bridge::SessionRunner runner(loopback_vehicle(), session_config(), 0, sink);
  runner.start();
  const auto deadline = Steady::now() + 2s;
  while (sink.links == 0 && Steady::now() < deadline) {
    std::this_thread::sleep_for(5ms);
  }
  // With no vehicle the link is reported, as Lost, by the periodic publish.
  EXPECT_GT(sink.links, 0);
  EXPECT_FALSE(sink.connected);
  runner.stop();
  runner.stop();
}
