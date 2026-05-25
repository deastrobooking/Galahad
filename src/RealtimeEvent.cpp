#include "galahad/RealtimeEvent.h"

namespace galahad
{
namespace
{
std::optional<int32_t> intArg(const Command& command, size_t index)
{
    if (index >= command.arguments.size())
        return std::nullopt;

    if (const auto* value = std::get_if<int32_t>(&command.arguments[index]))
        return *value;

    return std::nullopt;
}

std::optional<float> floatArg(const Command& command, size_t index)
{
    if (index >= command.arguments.size())
        return std::nullopt;

    if (const auto* value = std::get_if<float>(&command.arguments[index]))
        return *value;

    return std::nullopt;
}
} // namespace

std::optional<RealtimeEvent> toRealtimeEvent(const Command& command)
{
    RealtimeEvent event;

    if (command.address == "/galahad/event/clip_fired")
    {
        const auto track = intArg(command, 0);
        const auto clip = intArg(command, 1);
        if (!track || !clip || *track < 0 || *clip < 0)
            return std::nullopt;

        event.type = RealtimeEventType::ClipFired;
        event.trackIndex = static_cast<uint32_t>(*track);
        event.clipIndex = static_cast<uint32_t>(*clip);
        return event;
    }

    if (command.address == "/galahad/event/track_volume")
    {
        const auto track = intArg(command, 0);
        const auto value = floatArg(command, 1);
        if (!track || !value || *track < 0)
            return std::nullopt;

        event.type = RealtimeEventType::TrackVolumeChanged;
        event.trackIndex = static_cast<uint32_t>(*track);
        event.value = *value;
        return event;
    }

    if (command.address == "/galahad/event/song_started")
    {
        event.type = RealtimeEventType::SongStarted;
        return event;
    }

    if (command.address == "/galahad/event/song_stopped")
    {
        event.type = RealtimeEventType::SongStopped;
        return event;
    }

    if (command.address == "/galahad/event/tempo")
    {
        const auto value = floatArg(command, 0);
        if (!value)
            return std::nullopt;

        event.type = RealtimeEventType::TempoChanged;
        event.value = *value;
        return event;
    }

    if (command.address == "/galahad/event/transport")
    {
        const auto beat = floatArg(command, 0);
        const auto tempo = floatArg(command, 1);
        if (!beat || !tempo)
            return std::nullopt;

        event.type = RealtimeEventType::TransportSync;
        event.beatTime = static_cast<double>(*beat);
        event.value = *tempo;
        return event;
    }

    return std::nullopt;
}

std::string_view toString(RealtimeEventType type)
{
    switch (type)
    {
        case RealtimeEventType::ClipFired:
            return "ClipFired";
        case RealtimeEventType::TrackVolumeChanged:
            return "TrackVolumeChanged";
        case RealtimeEventType::SongStarted:
            return "SongStarted";
        case RealtimeEventType::SongStopped:
            return "SongStopped";
        case RealtimeEventType::TempoChanged:
            return "TempoChanged";
        case RealtimeEventType::TransportSync:
            return "TransportSync";
        case RealtimeEventType::Unknown:
        default:
            return "Unknown";
    }
}
} // namespace galahad
