#include "skyrelay/dds/domain.hpp"

#include <dds/dds.h>

#include <stdexcept>
#include <string>

namespace skyrelay::dds {

std::string cyclone_config_xml(const DomainConfig& c) {
  std::string x = "<CycloneDDS><Domain id=\"" + std::to_string(c.domain_id) + "\">";
  if (!c.allow_network) {
    x +=
        "<General>"
        "<Interfaces><NetworkInterface address=\"127.0.0.1\" multicast=\"false\"/>"
        "</Interfaces>"
        "<AllowMulticast>false</AllowMulticast>"
        "</General>"
        "<Discovery>"
        "<ParticipantIndex>auto</ParticipantIndex>"
        "<MaxAutoParticipantIndex>64</MaxAutoParticipantIndex>"
        "<Peers><Peer address=\"127.0.0.1\"/></Peers>"
        "</Discovery>";
  }
  x += "</Domain></CycloneDDS>";
  return x;
}

::dds::domain::DomainParticipant open_participant(const DomainConfig& c) {
  const auto xml = cyclone_config_xml(c);
  const dds_return_t rc = dds_create_domain(c.domain_id, xml.c_str());
  // Already created earlier in this process: reuse it.
  if (rc < 0 && rc != DDS_RETCODE_PRECONDITION_NOT_MET) {
    throw std::runtime_error("dds_create_domain failed: " +
                             std::string(dds_strretcode(rc)));
  }
  return ::dds::domain::DomainParticipant(c.domain_id);
}

::dds::pub::Publisher make_publisher(::dds::domain::DomainParticipant& dp,
                                     const DomainConfig& c) {
  ::dds::pub::qos::PublisherQos q;
  q << ::dds::core::policy::Partition(c.partition);
  return ::dds::pub::Publisher(dp, q);
}

::dds::sub::Subscriber make_subscriber(::dds::domain::DomainParticipant& dp,
                                       const DomainConfig& c) {
  ::dds::sub::qos::SubscriberQos q;
  q << ::dds::core::policy::Partition(c.partition);
  return ::dds::sub::Subscriber(dp, q);
}

}  // namespace skyrelay::dds
