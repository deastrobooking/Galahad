#include "galahad/MidiToolsEngine.h"

#include <algorithm>

namespace galahad
{
void MidiToolsEngine::setConfig(const MidiToolsEngineConfig& config) noexcept
{
    config_ = config;
    config_.bpm = std::clamp(config_.bpm, 20.0, 999.0);

    applySequencerConfig(config_.sequencer);
    lfo_.setConfig(config_.lfo);
    applyRouterConfig(config_.router);
}

void MidiToolsEngine::reset() noexcept
{
    sequencer_.reset();
    lfo_.reset();
}

size_t MidiToolsEngine::process(std::span<const MidiEvent> input,
                                int sampleRate,
                                int numSamples,
                                std::span<MidiEvent> output) noexcept
{
    if (output.empty())
        return 0;

    std::array<MidiEvent, MaxBlockEvents> routed{};
    std::array<MidiEvent, MaxBlockEvents> sequenced{};
    std::array<MidiEvent, MaxBlockEvents> lfoEvents{};
    std::array<MidiEvent, MaxBlockEvents> mergedA{};

    const size_t routedCount = router_.route(input, std::span<MidiEvent>(routed.data(), routed.size()));
    const size_t sequencedCount = sequencer_.process(config_.bpm,
                                                     sampleRate,
                                                     numSamples,
                                                     std::span<MidiEvent>(sequenced.data(), sequenced.size()));
    const size_t lfoCount = lfo_.process(sampleRate,
                                         numSamples,
                                         std::span<MidiEvent>(lfoEvents.data(), lfoEvents.size()));

    const size_t mergedACount = router_.merge(std::span<const MidiEvent>(routed.data(), routedCount),
                                              std::span<const MidiEvent>(sequenced.data(), sequencedCount),
                                              std::span<MidiEvent>(mergedA.data(), mergedA.size()));

    return router_.merge(std::span<const MidiEvent>(mergedA.data(), mergedACount),
                         std::span<const MidiEvent>(lfoEvents.data(), lfoCount),
                         output);
}

void MidiToolsEngine::applySequencerConfig(const SequencerModuleConfig& config) noexcept
{
    SequencerConfig sequencerConfig;
    sequencerConfig.channel = std::clamp<uint8_t>(config.channel, 1, 16);
    sequencerConfig.channelMode = config.channelMode;
    sequencerConfig.rootNote = clampMidi(config.rootNote);
    sequencerConfig.steps = std::clamp<uint8_t>(config.steps, 1, static_cast<uint8_t>(AlgorithmicSequencer::MaxSteps));
    sequencerConfig.rateDivisor = std::max(0.25, config.rateDivisor);
    sequencerConfig.running = config.enabled;

    sequencer_.setConfig(sequencerConfig);
    sequencer_.setEuclideanPattern(std::min(config.pulses, sequencerConfig.steps),
                                   sequencerConfig.steps,
                                   config.rotation,
                                   sequencerConfig.rootNote);

    for (uint8_t i = 0; i < sequencerConfig.steps; ++i)
    {
        SequencerStep step = sequencer_.step(i);
        if (step.enabled)
        {
            step.velocity = clampMidi(config.velocity);
            step.gatePercent = std::clamp<uint8_t>(config.gatePercent, 1, 100);
            step.probability = std::clamp<uint8_t>(config.probability, 0, 127);
        }
        sequencer_.setStep(i, step);
    }
}

void MidiToolsEngine::applyRouterConfig(const RouterModuleConfig& config) noexcept
{
    router_.clearRules();
    if (!config.enabled)
        return;

    MidiRouteRule rule;
    rule.enabled = true;
    rule.inputChannel = std::clamp<uint8_t>(config.inputChannel, 0, 16);
    rule.outputChannel = std::clamp<uint8_t>(config.outputChannel, 1, 16);
    rule.transpose = std::clamp(config.transpose, -48, 48);
    rule.velocityOffset = std::clamp(config.velocityOffset, -127, 127);
    rule.passNotes = config.passNotes;
    rule.passControlChanges = config.passControlChanges;
    router_.setRule(0, rule);
}

uint8_t MidiToolsEngine::clampMidi(int value) noexcept
{
    return static_cast<uint8_t>(std::clamp(value, 0, 127));
}
} // namespace galahad
