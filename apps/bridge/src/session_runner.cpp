#include "session_runner.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <exception>
#include <string>

namespace skyrelay::bridge {

namespace asio = boost::asio;
using namespace std::chrono_literals;

namespace {

constexpr auto kTick = 10ms;

const char* link_name(mav::LinkState s) {
  switch (s) {
    case mav::LinkState::Lost:
      return "LOST";
    case mav::LinkState::Recovering:
      return "RECOVERING";
    case mav::LinkState::Connected:
      return "CONNECTED";
    case mav::LinkState::Degraded:
      return "DEGRADED";
  }
  return "?";
}

const char* autopilot_name(vehicle::Autopilot a) {
  switch (a) {
    case vehicle::Autopilot::Px4:
      return "PX4";
    case vehicle::Autopilot::ArduPilot:
      return "ArduPilot";
    case vehicle::Autopilot::Other:
      return "other";
    case vehicle::Autopilot::Unknown:
      break;
  }
  return "unknown";
}

const char* type_name(vehicle::VehicleType t) {
  switch (t) {
    case vehicle::VehicleType::Multirotor:
      return "multirotor";
    case vehicle::VehicleType::FixedWing:
      return "fixed wing";
    case vehicle::VehicleType::Vtol:
      return "VTOL";
    case vehicle::VehicleType::Other:
      return "other type";
    case vehicle::VehicleType::Unknown:
      break;
  }
  return "unknown type";
}

}  // namespace

void SessionRunner::LoggingSink::vehicle_info(uint32_t id,
                                              const vehicle::InfoData& d,
                                              vehicle::Stamp s) {
  spdlog::info("{}: autopilot {} {}, sysid {} compid {}, {}", label_,
               autopilot_name(d.autopilot),
               d.firmware_version.value_or("(version unknown)"), d.mav_sysid,
               d.mav_compid, type_name(d.vehicle_type));
  next_.vehicle_info(id, d, s);
}

void SessionRunner::LoggingSink::link_status(uint32_t id,
                                             const vehicle::LinkStatusData& d,
                                             vehicle::Stamp s) {
  // The session starts Lost, so the first line is the first real change.
  if (d.state != last_state_) {
    last_state_ = d.state;
    const bool bad = d.state == mav::LinkState::Lost ||
                     d.state == mav::LinkState::Degraded;
    spdlog::log(bad ? spdlog::level::warn : spdlog::level::info,
                "{}: link {} (rx {:.0f} Hz, loss {:.1f} %)", label_,
                link_name(d.state), d.stats.rx_rate_hz, d.stats.loss_pct);
  }
  next_.link_status(id, d, s);
}

SessionRunner::SessionRunner(const VehicleConfig& vehicle,
                             const vehicle::SessionConfig& cfg,
                             mav::Channel ch, vehicle::TelemetrySink& sink)
    : label_("vehicle " + std::to_string(vehicle.id) + " (" + vehicle.name +
             ")"),
      log_sink_(label_, sink),
      timer_(io_) {
  link_ = std::make_unique<mav::UdpLink>(
      io_, vehicle.link, [this](std::span<const uint8_t> bytes) {
        if (!stopping_) session_->on_bytes(bytes);
      });
  transport_.link = link_.get();
  port_ = link_->local_port();
  session_ = std::make_unique<vehicle::VehicleSession>(cfg, ch, clock_,
                                                       transport_, log_sink_);
}

SessionRunner::~SessionRunner() { stop(); }

void SessionRunner::start() {
  if (thread_.joinable() || stopping_) return;
  next_tick_ = std::chrono::steady_clock::now();
  arm_timer();
  thread_ = std::thread([this] { run(); });
}

void SessionRunner::stop() {
  // Closing the socket and cancelling the timer leaves the io_context with
  // nothing to do, so run() returns once the aborted handlers have run.
  auto shutdown = [this] {
    stopping_ = true;
    timer_.cancel();
    transport_.link = nullptr;
    link_.reset();
  };
  if (!thread_.joinable()) {
    shutdown();  // never started, or already stopped: no other thread
    return;
  }
  asio::post(io_, shutdown);
  thread_.join();
}

void SessionRunner::run() {
  // A throwing handler must not take the vehicle down for good: log it and
  // keep serving.
  for (;;) {
    try {
      io_.run();
      return;
    } catch (const std::exception& e) {
      spdlog::error("{}: {}", label_, e.what());
    }
  }
}

void SessionRunner::arm_timer() {
  timer_.expires_at(next_tick_);
  timer_.async_wait([this](const boost::system::error_code& ec) {
    if (ec || stopping_) return;
    on_timer();
  });
}

void SessionRunner::on_timer() {
  session_->tick();

  // Another system on this vehicle's port. Logged at the first frame and each
  // time the count doubles, so it is visible without flooding the log.
  const uint64_t foreign = session_->counters().foreign_sysid_frames;
  if (foreign > 0 && foreign >= 2 * foreign_logged_) {
    spdlog::warn("{}: dropped {} frames from other system ids so far", label_,
                 foreign);
    foreign_logged_ = foreign;
  }

  const uint64_t reboots = session_->counters().reboots;
  if (reboots != reboots_logged_) {
    spdlog::warn("{}: vehicle restarted without losing the link, setting it "
                 "up again",
                 label_);
    reboots_logged_ = reboots;
  }

  // Fixed rate; after a stall, carry on from now rather than catch up.
  next_tick_ += kTick;
  const auto now = std::chrono::steady_clock::now();
  if (next_tick_ <= now) next_tick_ = now + kTick;
  arm_timer();
}

}  // namespace skyrelay::bridge
