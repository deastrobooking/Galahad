#pragma once

#include "galahad/MidiEvent.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace galahad
{
struct SequencerStep
{
    bool enabled{ false };
    uint8_t note{ 60 };
    uint8_t velocity{ 96 };
    uint8_t gatePercent{ 50 };
    int8_t transpose{ 0 };
    uint8_t probability{ 127 };
};

struct SequencerConfig
{
    uint8_t channel{ 1 };
    uint8_t rootNote{ 60 };
    uint8_t steps{ 16 };
    double rateDivisor{ 4.0 };
    bool running{ true };
};

class AlgorithmicSequencer
{
public:
    static constexpr size_t MaxSteps = 32;
    static constexpr size_t MaxEventsPerBlock = MaxSteps * 2;

    void setConfig(const SequencerConfig& config) noexcept;
    const SequencerConfig& config() const noexcept { return config_; }

    void setStep(size_t index, const SequencerStep& step) noexcept;
    const SequencerStep& step(size_t index) const noexcept;
    void setEuclideanPattern(uint8_t pulses, uint8_t steps, uint8_t rotation, uint8_t baseNote) noexcept;
    void reset() noexcept;

    size_t process(double bpm, int sampleRate, int numSamples, std::span<MidiEvent> output) noexcept;

private:
    static uint32_t nextRandom(uint32_t& state) noexcept;
    static uint8_t clampMidi(int value) noexcept;

    SequencerConfig config_{};
    std::array<SequencerStep, MaxSteps> steps_{};
    size_t stepIndex_{ 0 };
    int samplesUntilNextStep_{ 0 };
    bool hasPendingNoteOff_{ false };
    uint8_t pendingNote_{ 0 };
    int pendingNoteOffSamples_{ 0 };
    uint32_t randomState_{ 0x4d595df4u };
};
} // namespace galahad
