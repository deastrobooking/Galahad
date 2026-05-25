#include "GalahadMidiToolsProcessor.h"

GalahadMidiToolsProcessor::GalahadMidiToolsProcessor()
    : juce::AudioProcessor(BusesProperties())
{
}

void GalahadMidiToolsProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
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

    for (const auto metadata : midiMessages)
    {
        if (auto cell = galahad::midi::MidiProtocol::sessionCellForMessage(metadata.getMessage()))
        {
            launchedCells_.tryPush(*cell);
            lastTrack_.store(cell->track, std::memory_order_relaxed);
            lastScene_.store(cell->scene, std::memory_order_relaxed);
            launchSerial_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    galahad::midi::SessionCell cell;
    while (launchedCells_.tryPop(cell))
        midiMessages.addEvent(galahad::midi::MidiProtocol::clipFeedbackNote(cell.track, cell.scene, 127), 0);
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GalahadMidiToolsProcessor();
}
