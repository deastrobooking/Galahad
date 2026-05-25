#pragma once

#include "galahad/AlgorithmicSequencer.h"
#include "galahad/MidiLfo.h"
#include "galahad/MidiRouting.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace galahad
{
struct SequencerModuleConfig
{
    bool enabled{ true };
    uint8_t channel{ 1 };
    SequencerChannelMode channelMode{ SequencerChannelMode::Fixed };
    uint8_t rootNote{ 48 };
    uint8_t steps{ 16 };
    uint8_t pulses{ 5 };
    uint8_t rotation{ 0 };
    uint8_t velocity{ 96 };
    uint8_t gatePercent{ 45 };
    uint8_t probability{ 127 };
    double rateDivisor{ 4.0 };
};

struct RouterModuleConfig
{
    bool enabled{ true };
    uint8_t inputChannel{ 0 };
    uint8_t outputChannel{ 1 };
    int transpose{ 0 };
    int velocityOffset{ 0 };
    bool passNotes{ true };
    bool passControlChanges{ true };
};

struct MidiToolsEngineConfig
{
    double bpm{ 120.0 };
    SequencerModuleConfig sequencer{};
    MidiLfoConfig lfo{};
    RouterModuleConfig router{};
};

class MidiToolsEngine
{
public:
    static constexpr size_t MaxBlockEvents = 512;

    void setConfig(const MidiToolsEngineConfig& config) noexcept;
    const MidiToolsEngineConfig& config() const noexcept { return config_; }

    void reset() noexcept;

    size_t process(std::span<const MidiEvent> input,
                   int sampleRate,
                   int numSamples,
                   std::span<MidiEvent> output) noexcept;

private:
    void applySequencerConfig(const SequencerModuleConfig& config) noexcept;
    void applyRouterConfig(const RouterModuleConfig& config) noexcept;

    static uint8_t clampMidi(int value) noexcept;

    MidiToolsEngineConfig config_{};
    AlgorithmicSequencer sequencer_{};
    MidiLfo lfo_{};
    MidiMergerRouter router_{};
};
} // namespace galahad
