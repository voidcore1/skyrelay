#include "skyrelay/mav/link_monitor.hpp"

#include <algorithm>
#include <utility>

namespace skyrelay::mav {

namespace {
// A forward sequence step larger than this is taken as a restart of the
// sender rather than as a burst of lost frames.
constexpr unsigned kMaxForwardStep = 128;
}  // namespace

LinkMonitor::LinkMonitor(LinkThresholds thresholds) : th_(thresholds) {}

std::optional<LinkTransition> LinkMonitor::go(LinkState to, MonoTime now,
                                              std::string reason) {
  LinkTransition tr{state_, to, now, std::move(reason)};
  state_ = to;
  state_since_ = now;
  return tr;
}

void LinkMonitor::prune(MonoTime now) {
  while (!window_.empty() && window_.front().at < now - th_.loss_window) {
    window_.pop_front();
  }
}

double LinkMonitor::loss_pct(MonoTime now) const {
  uint64_t expected = 0;
  uint64_t received = 0;
  for (const auto& b : window_) {
    if (b.at < now - th_.loss_window) continue;
    expected += b.expected;
    received += b.received;
  }
  if (expected == 0 || received >= expected) return 0.0;
  return 100.0 * static_cast<double>(expected - received) /
         static_cast<double>(expected);
}

std::optional<LinkTransition> LinkMonitor::on_message(MonoTime now,
                                                      uint8_t compid,
                                                      uint8_t seq) {
  std::optional<LinkTransition> tr;
  if (state_ == LinkState::Lost) {
    // Whatever we knew before the outage is stale.
    window_.clear();
    last_seq_.clear();
    setup_at_.reset();
    tr = go(LinkState::Recovering, now, "first message");
  }
  seen_ = true;
  last_rx_ = now;

  auto it = last_seq_.find(compid);
  if (it == last_seq_.end()) {
    last_seq_[compid] = seq;
    window_.push_back({now, 1, 1});
    return tr;
  }
  const auto step = static_cast<uint8_t>(seq - it->second);
  if (step == 0) return tr;  // duplicate
  it->second = seq;
  if (step > kMaxForwardStep) {
    window_.clear();
    window_.push_back({now, 1, 1});
    return tr;
  }
  window_.push_back({now, step, 1});
  prune(now);
  return tr;
}

void LinkMonitor::on_setup_complete(MonoTime now) {
  if (state_ == LinkState::Recovering) setup_at_ = now;
}

std::optional<LinkTransition> LinkMonitor::tick(MonoTime now) {
  if (state_ == LinkState::Lost) return std::nullopt;
  prune(now);
  const auto gap =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_rx_);
  if (gap >= th_.lost_gap) return go(LinkState::Lost, now, "gap");

  const bool gap_ok = gap < th_.degraded_gap;
  const bool loss_ok = loss_pct(now) <= th_.degraded_loss_pct;
  switch (state_) {
    case LinkState::Recovering:
      if (setup_at_ && now - *setup_at_ >= th_.recover_healthy && gap_ok &&
          loss_ok) {
        return go(LinkState::Connected, now, "setup complete, link healthy");
      }
      break;
    case LinkState::Connected:
      if (!gap_ok) return go(LinkState::Degraded, now, "gap");
      if (!loss_ok) return go(LinkState::Degraded, now, "loss");
      break;
    case LinkState::Degraded:
      if (gap_ok && loss_ok) return go(LinkState::Connected, now, "recovered");
      break;
    case LinkState::Lost:
      break;
  }
  return std::nullopt;
}

LinkStats LinkMonitor::stats(MonoTime now) const {
  uint64_t received = 0;
  for (const auto& b : window_) {
    if (b.at >= now - th_.loss_window) received += b.received;
  }
  const double secs =
      std::chrono::duration<double>(th_.loss_window).count();
  LinkStats s{};
  s.rx_rate_hz = static_cast<double>(received) / secs;
  s.loss_pct = loss_pct(now);
  s.last_rx_age =
      seen_ ? std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                    last_rx_)
            : std::chrono::milliseconds::max();
  return s;
}

}  // namespace skyrelay::mav
