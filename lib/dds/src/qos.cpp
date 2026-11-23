#include "skyrelay/dds/qos.hpp"

#include <stdexcept>
#include <string>

#include "skyrelay/dds/topics.hpp"

namespace skyrelay::dds {
namespace {

// Applies a profile to any QoS object that carries these policies.
template <class Qos>
Qos build(Profile p) {
  Qos q;
  q << ::dds::core::policy::Reliability::Reliable()
    << ::dds::core::policy::History::KeepLast(1);
  switch (p) {
    case Profile::TelemetryFast:
      q << ::dds::core::policy::Durability::Volatile()
        << ::dds::core::policy::Deadline(::dds::core::Duration::from_millisecs(200))
        << ::dds::core::policy::Liveliness::Automatic(
               ::dds::core::Duration::from_secs(1));
      break;
    case Profile::TelemetryReliable:
      q << ::dds::core::policy::Durability::Volatile()
        << ::dds::core::policy::Deadline(::dds::core::Duration::from_millisecs(1000));
      break;
    case Profile::StateLatched:
      q << ::dds::core::policy::Durability::TransientLocal();
      break;
  }
  return q;
}

}  // namespace

::dds::pub::qos::DataWriterQos writer_qos(Profile p) {
  return build<::dds::pub::qos::DataWriterQos>(p);
}

::dds::sub::qos::DataReaderQos reader_qos(Profile p) {
  return build<::dds::sub::qos::DataReaderQos>(p);
}

::dds::topic::qos::TopicQos topic_qos(Profile p) {
  return build<::dds::topic::qos::TopicQos>(p);
}

Profile profile_for(std::string_view topic) {
  if (topic == kTopicVehicleInfo || topic == kTopicLinkStatus) {
    return Profile::StateLatched;
  }
  if (topic == kTopicPose) return Profile::TelemetryFast;
  if (topic == kTopicVehicleState || topic == kTopicHealth) {
    return Profile::TelemetryReliable;
  }
  throw std::invalid_argument("no QoS profile for topic " + std::string(topic));
}

}  // namespace skyrelay::dds
