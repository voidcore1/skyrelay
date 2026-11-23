#include "skyrelay/mav/link_monitor.hpp"

#include <algorithm>
#include <utility>

namespace skyrelay::mav {

namespace {
// A forward sequence step larger than this is taken as a restart of the
// sender rather than as a burst of lost frames.
constexpr unsigned kMaxForwardStep = 128;
// Steps 240..255 (back by 1..16) are late or reordered frames.
constexpr unsigned kMinBackwardStep = 240;
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

double LinkMonitor::loss_pct(MonoTime now, uint64_t* expected_out) const {
  uint64_t expected = 0;
  uint64_t received = 0;
  for (const auto& b : window_) {
    if (b.at < now - th_.loss_window) continue;
    expected += b.expected;
    received += b.received;
  }
  if (expected_out) *expected_out = expected;
  if (expected == 0 || received >= expected) return 0.0;
  return 100.0 * static_cast<double>(expected - received) /
         static_cast<double>(expected);
}

bool LinkMonitor::loss_exceeded(MonoTime now) const {
  uint64_t expected = 0;
  const double pct = loss_pct(now, &expected);
  return expected >= th_.min_loss_samples && pct > th_.degraded_loss_pct;
}

std::optional<LinkTransition> LinkMonitor::on_message(MonoTime now,
                                                      uint8_t compid,
                                                      uint8_t seq) {
  std::optional<LinkTransition> tr;
  if (state_ == LinkState::Lost) {
    // Whatever we knew before the outage is stale.
    window_.clear();
    last_seq_.clear();
    healthy_since_.reset();
    tr = go(LinkState::Recovering, now, "first message");
  }
  // A long inter-arrival gap restarts the healthy period of a recovery.
  if (!tr && seen_ && healthy_since_ && now - last_rx_ >= th_.degraded_gap) {
    healthy_since_ = now;
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
  if (step >= kMinBackwardStep) {
    // Late or reordered: it fills a hole counted when its successor arrived.
    window_.push_back({now, 0, 1});
    return tr;
  }
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
  if (state_ == LinkState::Recovering) healthy_since_ = now;
}

std::optional<LinkTransition> LinkMonitor::tick(MonoTime now) {
  if (state_ == LinkState::Lost) return std::nullopt;
  prune(now);
  const auto gap =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_rx_);
  const auto lost_gap = state_ == LinkState::Recovering
                            ? th_.recovering_lost_gap
                            : th_.lost_gap;
  if (gap >= lost_gap) return go(LinkState::Lost, now, "gap");

  const bool gap_ok = gap < th_.degraded_gap;
  const bool loss_ok = !loss_exceeded(now);
  switch (state_) {
    case LinkState::Recovering:
      if (healthy_since_ && !loss_ok) healthy_since_ = now;
      if (healthy_since_ && now - *healthy_since_ >= th_.recover_healthy &&
          gap_ok && loss_ok) {
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
