#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <dds/dds.hpp>

#include "skyrelay/dds/domain.hpp"

namespace skyrelay::gateway {

// Reads the five M1 telemetry topics with their contract QoS and hands every
// valid sample to a sink as JSON, and every instance that stops being alive
// (its writer was deleted, disposed it or lost its liveliness) to a gone sink.
// Both run on Cyclone's listener thread, where the JSON is built, so they have
// to be quick and must not block; the gateway's sinks only post to the
// io_context.
class DdsFeed {
 public:
  // topic is the protocol name (the last part of the DDS topic name, e.g.
  // "pose"); json is the sample's data object; t_bridge_ns comes from its
  // header.
  using Sink = std::function<void(std::string_view topic, uint32_t vehicle_id,
                                  std::string json, uint64_t t_bridge_ns)>;
  using GoneSink = std::function<void(std::string_view topic, uint32_t vehicle_id)>;

  DdsFeed(::dds::domain::DomainParticipant& participant,
          const skyrelay::dds::DomainConfig& config, Sink sink, GoneSink gone);
  // Detaches every listener before the readers go; a callback already running
  // finishes first.
  ~DdsFeed();
  DdsFeed(const DdsFeed&) = delete;
  DdsFeed& operator=(const DdsFeed&) = delete;

  // Protocol names of the topics with the state_latched QoS. A late-joining
  // DDS reader gets their last value from a writer that is still there, and
  // the gateway gives a new WebSocket client the same.
  static std::vector<std::string> latched_topics();

  // Samples that could not be converted (logged once).
  uint64_t errors() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace skyrelay::gateway
