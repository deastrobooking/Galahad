#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace galahad
{
using OscArgument = std::variant<int32_t, float, std::string>;

struct Command
{
    std::string address;
    std::vector<OscArgument> arguments;

    static Command clipFire(int32_t trackIndex, int32_t clipIndex)
    {
        return Command{ "/live/clip/fire", { trackIndex, clipIndex } };
    }

    static Command trackVolume(int32_t trackIndex, float value)
    {
        return Command{ "/live/track/set/volume", { trackIndex, value } };
    }

    static Command songTempo(float bpm)
    {
        return Command{ "/live/song/set/tempo", { bpm } };
    }

    static Command sceneFire(int32_t sceneIndex)
    {
        return Command{ "/live/scene/fire", { sceneIndex } };
    }

    static Command stopAllClips()
    {
        return Command{ "/live/song/stop_all_clips", {} };
    }

    static Command songStart()
    {
        return Command{ "/live/song/start_playing", {} };
    }

    static Command songStop()
    {
        return Command{ "/live/song/stop_playing", {} };
    }

    static Command clipFiredEvent(int32_t trackIndex, int32_t clipIndex)
    {
        return Command{ "/galahad/event/clip_fired", { trackIndex, clipIndex } };
    }

    static Command trackVolumeEvent(int32_t trackIndex, float value)
    {
        return Command{ "/galahad/event/track_volume", { trackIndex, value } };
    }

    static Command songStartedEvent()
    {
        return Command{ "/galahad/event/song_started", {} };
    }

    static Command songStoppedEvent()
    {
        return Command{ "/galahad/event/song_stopped", {} };
    }

    static Command tempoEvent(float bpm)
    {
        return Command{ "/galahad/event/tempo", { bpm } };
    }
};
} // namespace galahad
