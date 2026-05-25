#include "GalahadMidiToolsProcessor.h"

#include <array>

GalahadMidiToolsProcessor::GalahadMidiToolsProcessor()
    : juce::AudioProcessor(BusesProperties())
{
    galahad::SequencerConfig seqConfig;
    seqConfig.channel = 1;
    seqConfig.rootNote = 48;
    seqConfig.steps = 16;
    seqConfig.rateDivisor = 4.0;
    seqConfig.running = true;
    sequencer_.setConfig(seqConfig);
    sequencer_.setEuclideanPattern(5, 16, 0, seqConfig.rootNote);

    galahad::MidiLfoConfig lfoConfig;
    lfoConfig.channel = 1;
    lfoConfig.controller = 74;
    lfoConfig.minimum = 24;
    lfoConfig.maximum = 104;
    lfoConfig.rateHz = 0.2;
    lfoConfig.shape = galahad::LfoShape::Triangle;
    lfo_.setConfig(lfoConfig);

    router_.clearRules();
    galahad::MidiRouteRule passThrough;
    passThrough.enabled = true;
    passThrough.inputChannel = 0;
    passThrough.outputChannel = 1;
    router_.setRule(0, passThrough);
}

void GalahadMidiToolsProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    sampleRate_ = sampleRate;
    sequencer_.reset();
    lfo_.reset();
    launchedCells_.reset();
}

void GalahadMidiToolsProcessor::releaseResources()
{
    launchedCells_.reset();
}

bool GalahadMidiToolsProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet().isDisabled();
}

void GalahadMidiToolsProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();

    std::array<galahad::MidiEvent, MaxBlockEvents> incoming{};
    std::array<galahad::MidiEvent, MaxBlockEvents> routed{};
    std::array<galahad::MidiEvent, MaxBlockEvents> generated{};
    std::array<galahad::MidiEvent, MaxBlockEvents> lfoEvents{};
    std::array<galahad::MidiEvent, MaxBlockEvents> mergedA{};
    std::array<galahad::MidiEvent, MaxBlockEvents> mergedB{};

    size_t incomingCount = 0;
    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        if ((message.isNoteOnOrOff() || message.isController()) && incomingCount < incoming.size())
            incoming[incomingCount++] = fromJuceMidi(message, metadata.samplePosition);

        if (auto cell = galahad::midi::MidiProtocol::sessionCellForMessage(message))
        {
            launchedCells_.tryPush(*cell);
            lastTrack_.store(cell->track, std::memory_order_relaxed);
            lastScene_.store(cell->scene, std::memory_order_relaxed);
            launchSerial_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    const size_t routedCount = router_.route(std::span<const galahad::MidiEvent>(incoming.data(), incomingCount),
                                             std::span<galahad::MidiEvent>(routed.data(), routed.size()));
    size_t generatedCount = sequencer_.process(120.0,
                                               static_cast<int>(sampleRate_),
                                               buffer.getNumSamples(),
                                               std::span<galahad::MidiEvent>(generated.data(), generated.size()));
    const size_t lfoCount = lfo_.process(static_cast<int>(sampleRate_),
                                         buffer.getNumSamples(),
                                         std::span<galahad::MidiEvent>(lfoEvents.data(), lfoEvents.size()));

    galahad::midi::SessionCell cell;
    while (generatedCount < generated.size() && launchedCells_.tryPop(cell))
        generated[generatedCount++] = fromJuceMidi(galahad::midi::MidiProtocol::clipFeedbackNote(cell.track, cell.scene, 127), 0);

    const size_t mergedACount = router_.merge(std::span<const galahad::MidiEvent>(routed.data(), routedCount),
                                              std::span<const galahad::MidiEvent>(generated.data(), generatedCount),
                                              std::span<galahad::MidiEvent>(mergedA.data(), mergedA.size()));
    const size_t mergedBCount = router_.merge(std::span<const galahad::MidiEvent>(mergedA.data(), mergedACount),
                                              std::span<const galahad::MidiEvent>(lfoEvents.data(), lfoCount),
                                              std::span<galahad::MidiEvent>(mergedB.data(), mergedB.size()));

    midiMessages.clear();
    for (size_t i = 0; i < mergedBCount; ++i)
        midiMessages.addEvent(toJuceMidi(mergedB[i]), mergedB[i].sampleOffset);
}

juce::AudioProcessorEditor* GalahadMidiToolsProcessor::createEditor()
{
    return nullptr;
}

void GalahadMidiToolsProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    destData.reset();
}

void GalahadMidiToolsProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ignoreUnused(data, sizeInBytes);
}

galahad::MidiEvent GalahadMidiToolsProcessor::fromJuceMidi(const juce::MidiMessage& message, int sampleOffset) noexcept
{
    if (message.isNoteOn())
    {
        return galahad::MidiEvent{ galahad::MidiEventType::NoteOn,
                                   static_cast<uint8_t>(message.getChannel()),
                                   static_cast<uint8_t>(message.getNoteNumber()),
                                   static_cast<uint8_t>(message.getVelocity()),
                                   sampleOffset };
    }

    if (message.isNoteOff())
    {
        return galahad::MidiEvent{ galahad::MidiEventType::NoteOff,
                                   static_cast<uint8_t>(message.getChannel()),
                                   static_cast<uint8_t>(message.getNoteNumber()),
                                   0,
                                   sampleOffset };
    }

    if (message.isController())
    {
        return galahad::MidiEvent{ galahad::MidiEventType::ControlChange,
                                   static_cast<uint8_t>(message.getChannel()),
                                   static_cast<uint8_t>(message.getControllerNumber()),
                                   static_cast<uint8_t>(message.getControllerValue()),
                                   sampleOffset };
    }

    return galahad::MidiEvent{ galahad::MidiEventType::ControlChange, 1, 0, 0, sampleOffset };
}

juce::MidiMessage GalahadMidiToolsProcessor::toJuceMidi(const galahad::MidiEvent& event)
{
    switch (event.type)
    {
        case galahad::MidiEventType::NoteOn:
            return juce::MidiMessage::noteOn(event.channel, event.data1, event.data2);
        case galahad::MidiEventType::NoteOff:
            return juce::MidiMessage::noteOff(event.channel, event.data1);
        case galahad::MidiEventType::ControlChange:
        default:
            return juce::MidiMessage::controllerEvent(event.channel, event.data1, event.data2);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GalahadMidiToolsProcessor();
}
