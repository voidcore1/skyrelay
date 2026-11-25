#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>

#include <boost/asio.hpp>

#include "config.hpp"
#include "skyrelay/common/clock.hpp"
#include "skyrelay/mav/link.hpp"
#include "skyrelay/vehicle/session.hpp"
#include "skyrelay/vehicle/telemetry_sink.hpp"

namespace skyrelay::bridge {

// Runs one VehicleSession on its own thread and io_context, with the vehicle's
// link (UDP, TCP or serial) and a 10 ms tick timer. The session is only ever touched from that
// thread. The sink is shared with the other runners and must be thread-safe.
// An exception from the session or the sink, on a datagram or on a tick, is
// logged and that datagram or tick is lost; the next receive and the next tick
// still happen.
class SessionRunner {
 public:
  // Opens the link here, on the calling thread, so a UDP port that is taken
  // fails at startup. Throws boost::system::system_error for that and
  // std::invalid_argument for a bad channel. A TCP or serial link that cannot
  // connect yet keeps trying in the background.
  SessionRunner(const VehicleConfig& vehicle, const vehicle::SessionConfig& cfg,
                mav::Channel ch, vehicle::TelemetrySink& sink);
  ~SessionRunner();
  SessionRunner(const SessionRunner&) = delete;
  SessionRunner& operator=(const SessionRunner&) = delete;

  void start();
  // Cancels the timer, closes the link and joins the thread. Safe to call more
  // than once, and before start().
  void stop();

  // The bound UDP port, fixed at construction; 0 for TCP and serial links.
  uint16_t local_port() const { return port_; }

 private:
  // Logs the events an operator wants to see, then passes everything on.
  class LoggingSink final : public vehicle::TelemetrySink {
   public:
    LoggingSink(std::string label, vehicle::TelemetrySink& next)
        : label_(std::move(label)), next_(next) {}
    void vehicle_info(uint32_t id, const vehicle::InfoData& d,
                      vehicle::Stamp s) override;
    void vehicle_state(uint32_t id, const vehicle::StateData& d,
                       vehicle::Stamp s) override {
      next_.vehicle_state(id, d, s);
    }
    void pose(uint32_t id, const vehicle::PoseData& d,
              vehicle::Stamp s) override {
      next_.pose(id, d, s);
    }
    void health(uint32_t id, const vehicle::HealthData& d,
                vehicle::Stamp s) override {
      next_.health(id, d, s);
    }
    void link_status(uint32_t id, const vehicle::LinkStatusData& d,
                     vehicle::Stamp s) override;

   private:
    std::string label_;
    vehicle::TelemetrySink& next_;
    mav::LinkState last_state_ = mav::LinkState::Lost;
  };

  class LinkTransport final : public vehicle::Transport {
   public:
    void send(std::span<const uint8_t> bytes) override {
      if (link) link->send(bytes);
    }
    mav::Link* link = nullptr;
  };

  void arm_timer();
  void on_timer();
  void run();
  void log_handler_error(const char* where, const std::exception& e);

  std::string label_;
  boost::asio::io_context io_;
  SystemClock clock_;
  LoggingSink log_sink_;
  LinkTransport transport_;
  std::unique_ptr<mav::Link> link_;
  std::unique_ptr<vehicle::VehicleSession> session_;
  boost::asio::steady_timer timer_;
  std::chrono::steady_clock::time_point next_tick_{};
  uint16_t port_ = 0;
  uint64_t foreign_logged_ = 0;
  uint64_t reboots_logged_ = 0;
  uint64_t handler_errors_ = 0;
  uint64_t handler_errors_logged_ = 0;
  // Only touched on the runner thread, inside the posted stop handler and the
  // timer handler.
  bool stopping_ = false;
  std::thread thread_;
};

}  // namespace skyrelay::bridge
