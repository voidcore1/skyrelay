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
// valid sample to a sink as JSON. The sink runs on Cyclone's listener thread,
// where the JSON is built, so it has to be quick and must not block; the
// gateway's sink only posts to the io_context.
class DdsFeed {
 public:
  // topic is the protocol name (the last part of the DDS topic name, e.g.
  // "pose"); json is the sample's data object; t_bridge_ns comes from its
  // header.
  using Sink = std::function<void(std::string_view topic, uint32_t vehicle_id,
                                  std::string json, uint64_t t_bridge_ns)>;

  DdsFeed(::dds::domain::DomainParticipant& participant,
          const skyrelay::dds::DomainConfig& config, Sink sink);
  // Detaches every listener before the readers go; a callback already running
  // finishes first.
  ~DdsFeed();
  DdsFeed(const DdsFeed&) = delete;
  DdsFeed& operator=(const DdsFeed&) = delete;

  // Protocol names of the topics with the state_latched QoS. A late-joining
  // DDS reader gets their last value, and so does a new WebSocket client.
  static std::vector<std::string> latched_topics();

  // Samples that could not be converted (logged once).
  uint64_t errors() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace skyrelay::gateway
