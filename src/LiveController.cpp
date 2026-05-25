#include "galahad/LiveController.h"

#include "galahad/Command.h"

namespace galahad
{
LiveController::LiveController(std::unique_ptr<ITransport> transport)
    : transport_(std::move(transport))
{
}

bool LiveController::connect()
{
    return transport_ != nullptr && transport_->start();
}

void LiveController::disconnect()
{
    if (transport_ != nullptr)
        transport_->stop();
}

bool LiveController::fireClip(int32_t trackIndex, int32_t clipIndex)
{
    return transport_ != nullptr && transport_->send(Command::clipFire(trackIndex, clipIndex));
}

bool LiveController::fireScene(int32_t sceneIndex)
{
    return transport_ != nullptr && transport_->send(Command::sceneFire(sceneIndex));
}

bool LiveController::setTrackVolume(int32_t trackIndex, float value)
{
    return transport_ != nullptr && transport_->send(Command::trackVolume(trackIndex, value));
}

bool LiveController::setTempo(float bpm)
{
    return transport_ != nullptr && transport_->send(Command::songTempo(bpm));
}

bool LiveController::startSong()
{
    return transport_ != nullptr && transport_->send(Command::songStart());
}

bool LiveController::stopSong()
{
    return transport_ != nullptr && transport_->send(Command::songStop());
}

bool LiveController::stopAllClips()
{
    return transport_ != nullptr && transport_->send(Command::stopAllClips());
}

void LiveController::onEvent(CommandHandler handler)
{
    if (transport_ != nullptr)
        transport_->setHandler(std::move(handler));
}
} // namespace galahad
