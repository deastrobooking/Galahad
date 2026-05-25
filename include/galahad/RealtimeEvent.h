#pragma once

#include "galahad/Command.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace galahad
{
enum class RealtimeEventType : uint32_t
{
    Unknown = 0,
    ClipFired,
    TrackVolumeChanged,
    SongStarted,
    SongStopped,
    TempoChanged,
    TransportSync
};

struct RealtimeEvent
{
    RealtimeEventType type{ RealtimeEventType::Unknown };
    uint32_t trackIndex{ 0 };
    uint32_t clipIndex{ 0 };
    float value{ 0.0f };
    double beatTime{ 0.0 };
};

static_assert(std::is_trivially_copyable_v<RealtimeEvent>);

std::optional<RealtimeEvent> toRealtimeEvent(const Command& command);
std::string_view toString(RealtimeEventType type);
} // namespace galahad
