#include "dds_feed.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <exception>
#include <utility>

#include "skyrelay/dds/gen/v1_json.hpp"
#include "skyrelay/dds/idl.hpp"
#include "skyrelay/dds/qos.hpp"
#include "skyrelay/dds/topics.hpp"

namespace skyrelay::gateway {
namespace {

namespace sdds = skyrelay::dds;
namespace v1 = skyrelay::v1;

constexpr std::string_view kTopics[] = {sdds::kTopicVehicleInfo, sdds::kTopicVehicleState,
                                        sdds::kTopicPose, sdds::kTopicHealth,
                                        sdds::kTopicLinkStatus};

constexpr std::string_view protocol_name(std::string_view topic) {
  return topic.substr(topic.rfind('/') + 1);
}

// One topic: its reader and the listener that converts what it takes.
template <class T>
class Channel final : public ::dds::sub::NoOpDataReaderListener<T> {
 public:
  Channel(::dds::domain::DomainParticipant& dp, ::dds::sub::Subscriber& sub,
          std::string_view topic, const DdsFeed::Sink& sink, std::atomic<uint64_t>& errors)
      : name_(protocol_name(topic)),
        sink_(sink),
        errors_(errors),
        topic_(dp, std::string(topic), sdds::topic_qos(sdds::profile_for(topic))),
        reader_(sub, topic_, sdds::reader_qos(sdds::profile_for(topic)), this,
                ::dds::core::status::StatusMask::data_available()) {}

  ~Channel() override { detach(); }

  void detach() { reader_.listener(nullptr, ::dds::core::status::StatusMask::none()); }

  void on_data_available(::dds::sub::DataReader<T>& reader) override {
    // Nothing may escape into Cyclone's thread.
    try {
      auto samples = reader.take();
      for (const auto& s : samples) {
        if (!s.info().valid()) continue;
        const T& d = s.data();
        // Strings from a vehicle are not guaranteed to be UTF-8.
        sink_(name_, d.vehicle_id(),
              v1::to_json_value(d).dump(-1, ' ', false,
                                        nlohmann::json::error_handler_t::replace),
              d.header().t_bridge_ns());
      }
    } catch (const std::exception& e) {
      if (errors_.fetch_add(1) == 0) {
        spdlog::warn("dropping {} samples that fail to convert: {}", name_, e.what());
      }
    }
  }

 private:
  std::string_view name_;
  const DdsFeed::Sink& sink_;
  std::atomic<uint64_t>& errors_;
  ::dds::topic::Topic<T> topic_;
  ::dds::sub::DataReader<T> reader_;
};

}  // namespace

struct DdsFeed::Impl {
  Impl(::dds::domain::DomainParticipant& dp, const sdds::DomainConfig& dc, Sink s)
      : sink(std::move(s)),
        sub(sdds::make_subscriber(dp, dc)),
        info(dp, sub, sdds::kTopicVehicleInfo, sink, errors),
        state(dp, sub, sdds::kTopicVehicleState, sink, errors),
        pose(dp, sub, sdds::kTopicPose, sink, errors),
        health(dp, sub, sdds::kTopicHealth, sink, errors),
        link(dp, sub, sdds::kTopicLinkStatus, sink, errors) {}

  ~Impl() {
    // All listeners off before any reader, subscriber or the sink goes.
    link.detach();
    health.detach();
    pose.detach();
    state.detach();
    info.detach();
  }

  Sink sink;
  std::atomic<uint64_t> errors{0};
  ::dds::sub::Subscriber sub;
  Channel<v1::VehicleInfo> info;
  Channel<v1::VehicleState> state;
  Channel<v1::Pose> pose;
  Channel<v1::Health> health;
  Channel<v1::LinkStatus> link;
};

DdsFeed::DdsFeed(::dds::domain::DomainParticipant& participant,
                 const skyrelay::dds::DomainConfig& config, Sink sink)
    : impl_(std::make_unique<Impl>(participant, config, std::move(sink))) {}

DdsFeed::~DdsFeed() = default;

std::vector<std::string> DdsFeed::latched_topics() {
  std::vector<std::string> out;
  for (const auto topic : kTopics) {
    if (sdds::profile_for(topic) == sdds::Profile::StateLatched) {
      out.emplace_back(protocol_name(topic));
    }
  }
  return out;
}

uint64_t DdsFeed::errors() const { return impl_->errors.load(); }

}  // namespace skyrelay::gateway
