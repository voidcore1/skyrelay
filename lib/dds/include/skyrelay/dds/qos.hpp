#pragma once

#include <dds/dds.hpp>
#include <string_view>

namespace skyrelay::dds {

// The profiles from the contract. Writer, reader and topic QoS are
// built from the same table so they can never disagree (a reader asking for a
// shorter deadline than the writer offers would not match).
enum class Profile { TelemetryFast, TelemetryReliable, StateLatched, Command, CommandStatus };

::dds::pub::qos::DataWriterQos writer_qos(Profile);
::dds::sub::qos::DataReaderQos reader_qos(Profile);
::dds::topic::qos::TopicQos topic_qos(Profile);

// vehicle_info, link_status and mission_state are latched state, pose is fast
// telemetry, vehicle_state and health are reliable telemetry, command_request
// is Command and command_status is CommandStatus. Throws
// std::invalid_argument for a topic that is not a contract topic.
Profile profile_for(std::string_view topic);

}  // namespace skyrelay::dds
