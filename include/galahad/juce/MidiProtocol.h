#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cstdint>
#include <optional>

namespace galahad::midi
{
struct SessionCell
{
    int track = 0;
    int scene = 0;
};

enum class TransportAction : int
{
    Stop = 0,
    Play = 1,
    RecordToggle = 2
};

class MidiProtocol
{
public:
    static constexpr int ClipLaunchBaseNote = 36;
    static constexpr int SessionTracks = 8;
    static constexpr int SessionScenes = 8;

    static constexpr int MixerVolumeBaseCc = 20;
    static constexpr int MixerPanBaseCc = 28;
    static constexpr int MixerSendABaseCc = 36;
    static constexpr int MixerSendBBaseCc = 44;
    static constexpr int MixerSendCBaseCc = 52;
    static constexpr int MixerMuteBaseCc = 68;
    static constexpr int MixerSoloBaseCc = 76;
    static constexpr int MixerArmBaseCc = 84;
    static constexpr int MixerSelectBaseCc = 92;
    static constexpr int DeviceParamBaseCc = 100;
    static constexpr int DeviceBankLeftCc = 108;
    static constexpr int DeviceBankRightCc = 109;
    static constexpr int TransportCc = 110;
    static constexpr int TrackBankCc = 111;
    static constexpr int SceneBankCc = 112;
    static constexpr int RecordFeedbackCc = 113;
    static constexpr int MetronomeFeedbackCc = 114;
    static constexpr int SceneLaunchCc = 115;
    static constexpr int DeviceToggleCc = 116;
    static constexpr int PlayFeedbackCc = 117;
    static constexpr int TrackStopCc = 118;
    static constexpr int ClipSectionCc = 119;

    static constexpr uint8_t SysexManufacturer = 0x7d;
    static constexpr uint8_t SysexProjectTag0 = 0x47;
    static constexpr uint8_t SysexProjectTag1 = 0x48;
    static constexpr uint8_t SysexProtocolVersion = 0x01;

    static juce::MidiMessage clipLaunchNote(int track, int scene, int velocity = 100);
    static std::optional<SessionCell> sessionCellForMessage(const juce::MidiMessage& message);

    static juce::MidiMessage transportCc(TransportAction action);
    static juce::MidiMessage trackBankCc(int direction);
    static juce::MidiMessage sceneBankCc(int direction);

    static juce::MidiMessage clipFeedbackNote(int track, int scene, int velocity);
    static juce::MidiMessage feedbackSysex(int command, const juce::MemoryBlock& payload);
};
} // namespace galahad::midi
