#include "galahad/juce/MidiProtocol.h"

#include <vector>

namespace galahad::midi
{
namespace
{
int bankDirectionToValue(int direction)
{
    return direction < 0 ? 0 : 127;
}
} // namespace

juce::MidiMessage MidiProtocol::clipLaunchNote(int track, int scene, int velocity)
{
    const int safeTrack = juce::jlimit(0, SessionTracks - 1, track);
    const int safeScene = juce::jlimit(0, SessionScenes - 1, scene);
    const int note = ClipLaunchBaseNote + safeScene * SessionTracks + safeTrack;
    return juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(juce::jlimit(1, 127, velocity)));
}

std::optional<SessionCell> MidiProtocol::sessionCellForMessage(const juce::MidiMessage& message)
{
    if (!message.isNoteOn())
        return std::nullopt;

    const int note = message.getNoteNumber();
    const int maxNote = ClipLaunchBaseNote + SessionTracks * SessionScenes;
    if (note < ClipLaunchBaseNote || note >= maxNote)
        return std::nullopt;

    const int offset = note - ClipLaunchBaseNote;
    return SessionCell{ offset % SessionTracks, offset / SessionTracks };
}

juce::MidiMessage MidiProtocol::transportCc(TransportAction action)
{
    return juce::MidiMessage::controllerEvent(1, TransportCc, static_cast<int>(action));
}

juce::MidiMessage MidiProtocol::trackBankCc(int direction)
{
    return juce::MidiMessage::controllerEvent(1, TrackBankCc, bankDirectionToValue(direction));
}

juce::MidiMessage MidiProtocol::sceneBankCc(int direction)
{
    return juce::MidiMessage::controllerEvent(1, SceneBankCc, bankDirectionToValue(direction));
}

juce::MidiMessage MidiProtocol::clipFeedbackNote(int track, int scene, int velocity)
{
    const int safeVelocity = juce::jlimit(0, 127, velocity);
    if (safeVelocity == 0)
        return juce::MidiMessage::noteOff(1, ClipLaunchBaseNote + scene * SessionTracks + track);

    return clipLaunchNote(track, scene, safeVelocity);
}

juce::MidiMessage MidiProtocol::feedbackSysex(int command, const juce::MemoryBlock& payload)
{
    std::vector<juce::uint8> data;
    data.reserve(payload.getSize() + 7);
    data.push_back(0xf0);
    data.push_back(SysexManufacturer);
    data.push_back(SysexProjectTag0);
    data.push_back(SysexProjectTag1);
    data.push_back(SysexProtocolVersion);
    data.push_back(static_cast<juce::uint8>(juce::jlimit(0, 127, command)));

    const auto* bytes = static_cast<const juce::uint8*>(payload.getData());
    for (size_t i = 0; i < payload.getSize(); ++i)
        data.push_back(static_cast<juce::uint8>(bytes[i] & 0x7f));

    data.push_back(0xf7);
    return juce::MidiMessage(data.data(), static_cast<int>(data.size()));
}
} // namespace galahad::midi
