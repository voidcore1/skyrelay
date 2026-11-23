#pragma once

// The only place in SkyRelay that includes the MAVLink C headers. The channel
// count must be defined before the first include, and it must be the same in
// every translation unit, which is why nothing else includes them directly.
#ifndef MAVLINK_COMM_NUM_BUFFERS
#define MAVLINK_COMM_NUM_BUFFERS 16
#endif

#include <ardupilotmega/mavlink.h>

#include <cstdint>

namespace skyrelay::mav {

// MAVLink channel, 0-15. One channel per vehicle session per process (see
// ADR-0005). It indexes the library's static per-channel parser and sequence
// state, so two sessions that use different channels never share state.
using Channel = uint8_t;

inline constexpr Channel kMaxChannels = MAVLINK_COMM_NUM_BUFFERS;

}  // namespace skyrelay::mav
