#include "fanout.hpp"

#include <charconv>
#include <chrono>
#include <cmath>

namespace skyrelay::gateway {
namespace {

const std::chrono::nanoseconds kPosePeriod{std::llround(1e9 / kPoseMaxHz)};

// {"type":"sample","topic":T,"data":D,"age_ms":A}. age_ms is now minus the
// bridge's decode time, both CLOCK_REALTIME; it is negative if the two clocks
// disagree and null if the bridge time is unknown.
void append_sample(std::string& out, std::string_view topic, const std::string& data,
                   uint64_t t_bridge_ns, uint64_t now_ns) {
  out += R"({"type":"sample","topic":")";
  out += topic;
  out += R"(","data":)";
  out += data;
  out += R"(,"age_ms":)";
  if (t_bridge_ns == 0) {
    out += "null";
  } else {
    // Unsigned wrap-around makes this the signed difference.
    const auto diff_ns = static_cast<int64_t>(now_ns - t_bridge_ns);
    const double age_ms = static_cast<double>(diff_ns) / 1e6;
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof(buf), age_ms,
                                 std::chars_format::fixed, 3);
    out.append(buf, r.ptr);
  }
  out += '}';
}

}  // namespace

Fanout::Fanout(const Clock& clock, std::vector<std::string> latched_topics)
    : clock_(clock), latched_topics_(latched_topics.begin(), latched_topics.end()) {}

Fanout::ClientId Fanout::add_client(std::function<void(std::string)> send_async,
                                    std::function<bool()> write_in_progress) {
  const ClientId id = next_id_++;
  Client& c = clients_[id];
  c.send = std::move(send_async);
  c.busy = std::move(write_in_progress);
  for (const auto& [key, value] : latched_) {
    const auto it = slot(c, key);
    it->second.value = value;
    mark_dirty(c, it);
  }
  return id;
}

void Fanout::remove_client(ClientId id) { clients_.erase(id); }

void Fanout::publish(std::string_view topic, uint32_t vehicle_id, std::string json,
                     MonoTime now, uint64_t t_bridge_ns) {
  const Value v{std::make_shared<const std::string>(std::move(json)), t_bridge_ns};
  const SlotKey key{std::string(topic), vehicle_id};
  if (latched_topics_.contains(topic)) latched_[key] = v;
  for (auto& [id, c] : clients_) {
    offer(c, slot(c, key), v, now);
    flush(c);
  }
}

void Fanout::on_write_complete(ClientId id, MonoTime) {
  const auto it = clients_.find(id);
  if (it != clients_.end()) flush(it->second);
}

void Fanout::poll(MonoTime now) {
  for (auto& [id, c] : clients_) {
    if (c.held > 0) {
      for (auto it = c.slots.begin(); it != c.slots.end(); ++it) {
        Slot& s = it->second;
        if (s.held && now - s.arrived >= kPosePeriod && s.limiter->allow(now)) {
          s.held = false;
          --c.held;
          mark_dirty(c, it);
        }
      }
    }
    flush(c);
  }
}

Fanout::Stats Fanout::stats(ClientId id) const {
  const auto it = clients_.find(id);
  return it == clients_.end() ? Stats{} : it->second.stats;
}

Fanout::Slots::iterator Fanout::slot(Client& c, const SlotKey& key) {
  const auto [it, inserted] = c.slots.try_emplace(key);
  if (inserted && key.first == kPoseTopic) it->second.limiter.emplace(kPoseMaxHz);
  return it;
}

void Fanout::offer(Client& c, Slots::iterator it, const Value& v, MonoTime now) {
  Slot& s = it->second;
  // An unsent value is replaced: latest-value, never a queue.
  if (s.dirty || s.held) ++c.stats.dropped_superseded;
  s.value = v;
  s.arrived = now;
  // Already on its way at the next write; the limiter was consulted when it
  // became dirty, so replacing it does not raise the rate.
  if (s.dirty) return;
  if (!s.limiter || s.limiter->allow(now)) {
    if (s.held) {
      s.held = false;
      --c.held;
    }
    mark_dirty(c, it);
  } else if (!s.held) {
    s.held = true;
    ++c.held;
  }
}

void Fanout::mark_dirty(Client& c, Slots::iterator it) {
  if (it->second.dirty) return;
  it->second.dirty = true;
  c.dirty.push_back(it);
}

void Fanout::flush(Client& c) {
  if (c.dirty.empty() || c.busy()) return;
  const uint64_t now_ns = clock_.realtime_ns();
  std::string frame = "[";
  for (const auto it : c.dirty) {
    if (frame.size() > 1) frame += ',';
    const Slot& s = it->second;
    append_sample(frame, it->first.first, *s.value.json, s.value.t_bridge_ns, now_ns);
    it->second.dirty = false;
  }
  frame += ']';
  c.dirty.clear();
  c.send(std::move(frame));
}

}  // namespace skyrelay::gateway
