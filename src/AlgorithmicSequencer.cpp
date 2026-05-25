#include "galahad/AlgorithmicSequencer.h"

#include <algorithm>
#include <cmath>

namespace galahad
{
namespace
{
constexpr SequencerStep DisabledStep{};

int samplesPerStep(double bpm, int sampleRate, double rateDivisor)
{
    if (bpm <= 0.0 || sampleRate <= 0 || rateDivisor <= 0.0)
        return 1;

    const double beatsPerSecond = bpm / 60.0;
    const double stepsPerSecond = beatsPerSecond * rateDivisor;
    return std::max(1, static_cast<int>(std::round(static_cast<double>(sampleRate) / stepsPerSecond)));
}
} // namespace

void AlgorithmicSequencer::setConfig(const SequencerConfig& config) noexcept
{
    config_ = config;
    config_.channel = std::clamp<uint8_t>(config_.channel, 1, 16);
    config_.rootNote = clampMidi(config_.rootNote);
    config_.steps = std::clamp<uint8_t>(config_.steps, 1, static_cast<uint8_t>(MaxSteps));
    config_.rateDivisor = std::max(0.25, config_.rateDivisor);
    stepIndex_ %= config_.steps;
}

void AlgorithmicSequencer::setStep(size_t index, const SequencerStep& step) noexcept
{
    if (index >= MaxSteps)
        return;

    steps_[index] = step;
    steps_[index].note = clampMidi(step.note);
    steps_[index].velocity = clampMidi(step.velocity);
    steps_[index].gatePercent = std::clamp<uint8_t>(step.gatePercent, 1, 100);
    steps_[index].probability = std::clamp<uint8_t>(step.probability, 0, 127);
}

const SequencerStep& AlgorithmicSequencer::step(size_t index) const noexcept
{
    if (index >= MaxSteps)
        return DisabledStep;

    return steps_[index];
}

void AlgorithmicSequencer::setEuclideanPattern(uint8_t pulses, uint8_t steps, uint8_t rotation, uint8_t baseNote) noexcept
{
    steps = std::clamp<uint8_t>(steps, 1, static_cast<uint8_t>(MaxSteps));
    pulses = std::min(pulses, steps);
    config_.steps = steps;
    config_.rootNote = clampMidi(baseNote);

    for (uint8_t i = 0; i < steps; ++i)
    {
        const uint8_t rotated = static_cast<uint8_t>((i + rotation) % steps);
        const bool active = pulses > 0 && ((rotated * pulses) % steps) < pulses;
        SequencerStep next;
        next.enabled = active;
        next.note = clampMidi(baseNote + (active ? static_cast<int>((i % 4) * 2) : 0));
        next.velocity = static_cast<uint8_t>(active ? 96 : 0);
        next.gatePercent = 45;
        next.probability = 127;
        setStep(i, next);
    }

    for (size_t i = steps; i < MaxSteps; ++i)
        setStep(i, SequencerStep{});
}

void AlgorithmicSequencer::reset() noexcept
{
    stepIndex_ = 0;
    samplesUntilNextStep_ = 0;
    hasPendingNoteOff_ = false;
    pendingNote_ = 0;
    pendingNoteOffSamples_ = 0;
    randomState_ = 0x4d595df4u;
}

size_t AlgorithmicSequencer::process(double bpm, int sampleRate, int numSamples, std::span<MidiEvent> output) noexcept
{
    if (!config_.running || output.empty() || numSamples <= 0)
        return 0;

    const int stepSamples = samplesPerStep(bpm, sampleRate, config_.rateDivisor);
    size_t count = 0;
    int cursor = 0;

    if (hasPendingNoteOff_ && count < output.size())
    {
        if (pendingNoteOffSamples_ < numSamples)
        {
            output[count++] = MidiEvent{ MidiEventType::NoteOff, config_.channel, pendingNote_, 0, pendingNoteOffSamples_ };
            hasPendingNoteOff_ = false;
        }
        else
        {
            pendingNoteOffSamples_ -= numSamples;
        }
    }

    while (cursor < numSamples && count < output.size())
    {
        if (samplesUntilNextStep_ > 0)
        {
            const int advance = std::min(samplesUntilNextStep_, numSamples - cursor);
            samplesUntilNextStep_ -= advance;
            cursor += advance;
            continue;
        }

        const auto& current = steps_[stepIndex_];
        const uint32_t randomValue = nextRandom(randomState_) & 0x7f;
        if (current.enabled && randomValue <= current.probability)
        {
            const uint8_t note = clampMidi(static_cast<int>(current.note) + current.transpose);
            output[count++] = MidiEvent{ MidiEventType::NoteOn, config_.channel, note, current.velocity, cursor };

            const int gateSamples = std::max(1, (stepSamples * static_cast<int>(current.gatePercent)) / 100);
            if (count < output.size() && cursor + gateSamples < numSamples)
                output[count++] = MidiEvent{ MidiEventType::NoteOff, config_.channel, note, 0, cursor + gateSamples };
            else
            {
                hasPendingNoteOff_ = true;
                pendingNote_ = note;
                pendingNoteOffSamples_ = cursor + gateSamples - numSamples;
            }
        }

        stepIndex_ = (stepIndex_ + 1) % config_.steps;
        samplesUntilNextStep_ = stepSamples;
    }

    return count;
}

uint32_t AlgorithmicSequencer::nextRandom(uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

uint8_t AlgorithmicSequencer::clampMidi(int value) noexcept
{
    return static_cast<uint8_t>(std::clamp(value, 0, 127));
}
} // namespace galahad
