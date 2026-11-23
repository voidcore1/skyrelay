#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>

#include "skyrelay/common/clock.hpp"

namespace skyrelay::mav {

enum class LinkState : uint8_t { Lost, Recovering, Connected, Degraded };

struct LinkThresholds {
  std::chrono::milliseconds degraded_gap{250};
  std::chrono::milliseconds lost_gap{1000};
  std::chrono::milliseconds loss_window{1000};
  std::chrono::milliseconds recover_healthy{500};
  double degraded_loss_pct{20.0};
  // Loss only counts once the window holds at least this many expected frames.
  std::size_t min_loss_samples{10};
  // Before setup completes only a heartbeat may be flowing (1 Hz), so the
  // Recovering -> Lost gap must be well above lost_gap.
  std::chrono::milliseconds recovering_lost_gap{2500};
};

struct LinkTransition {
  LinkState from;
  LinkState to;
  MonoTime at;
  std::string reason;
};

struct LinkStats {
  double rx_rate_hz;
  double loss_pct;
  std::chrono::milliseconds last_rx_age;
};

// Decides the link state of one vehicle from the arrival times and sequence
// numbers of its frames. It never reads a clock or does I/O: time only comes
// in through the `now` arguments, so tests can drive it with a fake clock.
class LinkMonitor {
 public:
  explicit LinkMonitor(LinkThresholds thresholds = {});

  // Feed every valid frame. Returns the Lost -> Recovering transition on the
  // first frame after the link was lost; all other changes come from tick().
  std::optional<LinkTransition> on_message(MonoTime now, uint8_t compid,
                                           uint8_t seq);
  // The session has re-requested message intervals and re-read the mode.
  // Ignored unless Recovering: the caller restarts setup on every
  // Lost -> Recovering transition.
  void on_setup_complete(MonoTime now);
  // Call periodically; evaluates gaps, loss and the recovery timer. It must
  // run at least every 100 ms to meet the 1.5 s lost-detection target.
  std::optional<LinkTransition> tick(MonoTime now);

  LinkState state() const { return state_; }
  MonoTime state_since() const { return state_since_; }
  LinkStats stats(MonoTime now) const;

 private:
  struct Bucket {
    MonoTime at;
    uint32_t expected;
    uint32_t received;
  };

  void prune(MonoTime now);
  // Expected-frame total and loss percentage over the window at `now`.
  double loss_pct(MonoTime now, uint64_t* expected_out = nullptr) const;
  bool loss_exceeded(MonoTime now) const;
  std::optional<LinkTransition> go(LinkState to, MonoTime now,
                                   std::string reason);

  LinkThresholds th_;
  LinkState state_{LinkState::Lost};
  MonoTime state_since_{};
  MonoTime last_rx_{};
  bool seen_{false};
  std::optional<MonoTime> healthy_since_;
  std::map<uint8_t, uint8_t> last_seq_;
  std::deque<Bucket> window_;
};

}  // namespace skyrelay::mav
