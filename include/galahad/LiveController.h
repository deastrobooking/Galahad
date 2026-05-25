#pragma once

#include "galahad/Transport.h"

#include <memory>

namespace galahad
{
class LiveController
{
public:
    explicit LiveController(std::unique_ptr<ITransport> transport);

    bool connect();
    void disconnect();

    bool fireClip(int32_t trackIndex, int32_t clipIndex);
    bool fireScene(int32_t sceneIndex);
    bool setTrackVolume(int32_t trackIndex, float value);
    bool setTempo(float bpm);
    bool startSong();
    bool stopSong();
    bool stopAllClips();

    void onEvent(CommandHandler handler);

private:
    std::unique_ptr<ITransport> transport_;
};
} // namespace galahad
