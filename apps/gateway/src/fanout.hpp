#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "skyrelay/common/clock.hpp"
#include "skyrelay/common/rate_limiter.hpp"

namespace skyrelay::gateway {

// The topic as the WebSocket protocol names it: the last part of the DDS
// topic name.
inline constexpr std::string_view kPoseTopic = "pose";
// pose is capped per client per vehicle; every other topic goes out on arrival.
inline constexpr double kPoseMaxHz = 10.0;

// Fans telemetry out to WebSocket clients without ever waiting for one.
//
// Each client has one latest-value slot per (topic, vehicle). A new sample
// overwrites the slot, and if the old value was never sent it counts as
// superseded. A client has at most one write outstanding: while it is busy,
// samples only update its slots, and when the write completes every dirty slot
// goes out together as one frame, a JSON array of sample messages. A slow
// client therefore costs a bounded amount of memory and never delays anyone
// else.
//
// pose is capped at kPoseMaxHz per client per vehicle with a RateLimiter. A
// pose that arrives too early is held, and the next pose replaces it, so a
// steady stream goes out as it arrives, without added delay. A held pose that
// nothing replaces within one period (the stream paused or stopped) is sent by
// poll(), so a client always ends up with the last pose.
//
// Latched topics (those with the state_latched QoS) also keep one value per
// vehicle for the whole process, and a new client starts with those, the way
// a late-joining DDS reader gets them.
//
// Threading: not thread-safe. In the gateway every call happens on the
// io_context thread, and the callbacks run there too. The callbacks must not
// call back into the Fanout.
class Fanout {
 public:
  using ClientId = uint64_t;

  explicit Fanout(const Clock& clock, std::vector<std::string> latched_topics = {});

  // send_async starts writing one frame and returns at once; write_in_progress
  // says whether the previous frame is still being written. Neither is called
  // from add_client itself: the latched values go out on the client's first
  // on_write_complete() or poll().
  ClientId add_client(std::function<void(std::string)> send_async,
                      std::function<bool()> write_in_progress);
  void remove_client(ClientId);

  // `json` is the sample's data object, already serialised. t_bridge_ns is the
  // sample's header.t_bridge_ns (0 if unknown); it is kept with the slot so
  // that age_ms is computed when that sample is actually sent. Never blocks.
  void publish(std::string_view topic, uint32_t vehicle_id, std::string json,
               MonoTime now, uint64_t t_bridge_ns = 0);

  // The client's write finished: its dirty slots, if any, go out as one frame.
  void on_write_complete(ClientId, MonoTime now);

  // Releases held poses that nothing replaced within one period and flushes
  // idle clients. The gateway calls it from a timer.
  void poll(MonoTime now);

  struct Stats {
    uint64_t dropped_superseded = 0;
  };
  Stats stats(ClientId) const;

 private:
  struct Value {
    std::shared_ptr<const std::string> json;  // shared by every client
    uint64_t t_bridge_ns = 0;
  };
  struct Slot {
    Value value;
    MonoTime arrived{};
    bool dirty = false;  // waits to be sent
    bool held = false;   // throttled; sent when allowed or after a pause
    std::optional<RateLimiter> limiter;  // pose only
  };
  using SlotKey = std::pair<std::string, uint32_t>;
  using Slots = std::map<SlotKey, Slot>;
  struct Client {
    std::function<void(std::string)> send;
    std::function<bool()> busy;
    Slots slots;
    std::vector<Slots::iterator> dirty;  // in the order they became dirty
    size_t held = 0;
    Stats stats;
  };

  Slots::iterator slot(Client&, const SlotKey&);
  void offer(Client&, Slots::iterator, const Value&, MonoTime now);
  void mark_dirty(Client&, Slots::iterator);
  void flush(Client&);

  const Clock& clock_;
  std::set<std::string, std::less<>> latched_topics_;
  std::map<SlotKey, Value> latched_;
  std::map<ClientId, Client> clients_;
  ClientId next_id_ = 1;
};

}  // namespace skyrelay::gateway
