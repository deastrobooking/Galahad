#pragma once

#include "galahad/MidiEvent.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace galahad
{
enum class LfoShape : uint8_t
{
    Sine,
    Triangle,
    SawUp,
    SawDown,
    Square,
    RandomSampleHold
};

struct MidiLfoConfig
{
    bool enabled{ true };
    uint8_t channel{ 1 };
    uint8_t controller{ 1 };
    uint8_t minimum{ 0 };
    uint8_t maximum{ 127 };
    double rateHz{ 0.5 };
    LfoShape shape{ LfoShape::Sine };
};

class MidiLfo
{
public:
    static constexpr size_t MaxEventsPerBlock = 64;

    void setConfig(const MidiLfoConfig& config) noexcept;
    const MidiLfoConfig& config() const noexcept { return config_; }
    void reset() noexcept;

    size_t process(int sampleRate, int numSamples, std::span<MidiEvent> output) noexcept;

private:
    static uint32_t nextRandom(uint32_t& state) noexcept;
    static double shapeValue(LfoShape shape, double phase, uint32_t& randomState, double heldValue) noexcept;
    static uint8_t scaleToMidi(double value, uint8_t minimum, uint8_t maximum) noexcept;

    MidiLfoConfig config_{};
    double phase_{ 0.0 };
    double heldRandomValue_{ 0.0 };
    uint8_t lastValue_{ 255 };
    uint32_t randomState_{ 0x7f4a7c15u };
};
} // namespace galahad
