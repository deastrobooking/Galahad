#include "galahad/MidiLfo.h"

#include <algorithm>
#include <cmath>

namespace galahad
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
}

void MidiLfo::setConfig(const MidiLfoConfig& config) noexcept
{
    config_ = config;
    config_.channel = std::clamp<uint8_t>(config_.channel, 1, 16);
    config_.controller = std::clamp<uint8_t>(config_.controller, 0, 127);
    if (config_.minimum > config_.maximum)
        std::swap(config_.minimum, config_.maximum);
    config_.rateHz = std::clamp(config_.rateHz, 0.01, 100.0);
}

void MidiLfo::reset() noexcept
{
    phase_ = 0.0;
    lastValue_ = 255;
    randomState_ = 0x7f4a7c15u;
    heldRandomValue_ = static_cast<double>(nextRandom(randomState_) & 0xffffu) / 65535.0;
}

size_t MidiLfo::process(int sampleRate, int numSamples, std::span<MidiEvent> output) noexcept
{
    if (!config_.enabled || sampleRate <= 0 || numSamples <= 0 || output.empty())
        return 0;

    const int eventsToTry = std::min<int>(static_cast<int>(output.size()), std::max(1, numSamples / 32));
    size_t count = 0;

    for (int i = 0; i < eventsToTry; ++i)
    {
        const int sampleOffset = (i * numSamples) / eventsToTry;
        const double offsetPhase = std::fmod(phase_ + (static_cast<double>(sampleOffset) * config_.rateHz / sampleRate), 1.0);
        const double value = shapeValue(config_.shape, offsetPhase, randomState_, heldRandomValue_);
        const uint8_t midiValue = scaleToMidi(value, config_.minimum, config_.maximum);

        if (midiValue != lastValue_)
        {
            output[count++] = MidiEvent{ MidiEventType::ControlChange, config_.channel, config_.controller, midiValue, sampleOffset };
            lastValue_ = midiValue;
        }
    }

    const double nextPhase = phase_ + (static_cast<double>(numSamples) * config_.rateHz / sampleRate);
    if (config_.shape == LfoShape::RandomSampleHold && nextPhase >= 1.0)
        heldRandomValue_ = static_cast<double>(nextRandom(randomState_) & 0xffffu) / 65535.0;

    phase_ = std::fmod(nextPhase, 1.0);
    return count;
}

uint32_t MidiLfo::nextRandom(uint32_t& state) noexcept
{
    state = state * 1664525u + 1013904223u;
    return state;
}

double MidiLfo::shapeValue(LfoShape shape, double phase, uint32_t& randomState, double heldValue) noexcept
{
    (void)randomState;

    switch (shape)
    {
        case LfoShape::Triangle:
            return phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0;
        case LfoShape::SawUp:
            return phase;
        case LfoShape::SawDown:
            return 1.0 - phase;
        case LfoShape::Square:
            return phase < 0.5 ? 1.0 : 0.0;
        case LfoShape::RandomSampleHold:
            return heldValue;
        case LfoShape::Sine:
        default:
            return (std::sin(phase * Pi * 2.0) + 1.0) * 0.5;
    }
}

uint8_t MidiLfo::scaleToMidi(double value, uint8_t minimum, uint8_t maximum) noexcept
{
    const double clipped = std::clamp(value, 0.0, 1.0);
    const double scaled = static_cast<double>(minimum) + (static_cast<double>(maximum - minimum) * clipped);
    return static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::round(scaled)), 0, 127));
}
} // namespace galahad
