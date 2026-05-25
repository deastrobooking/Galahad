#pragma once

#include "galahad/MidiToolsEngine.h"
#include "galahad/SpscQueue.h"
#include "galahad/juce/MidiProtocol.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

class GalahadMidiToolsProcessor final : public juce::AudioProcessor
{
public:
    GalahadMidiToolsProcessor();
    ~GalahadMidiToolsProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    int lastTrack() const noexcept { return lastTrack_.load(std::memory_order_relaxed); }
    int lastScene() const noexcept { return lastScene_.load(std::memory_order_relaxed); }
    int launchSerial() const noexcept { return launchSerial_.load(std::memory_order_relaxed); }

private:
    static constexpr size_t MaxBlockEvents = galahad::MidiToolsEngine::MaxBlockEvents;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static galahad::MidiEvent fromJuceMidi(const juce::MidiMessage& message, int sampleOffset) noexcept;
    static juce::MidiMessage toJuceMidi(const galahad::MidiEvent& event);

    void updateEngineConfig(double bpm);
    void ensureVirtualMidiOutput();

    galahad::MidiToolsEngine engine_;
    juce::AudioProcessorValueTreeState parameters_;
    galahad::SpscQueue<galahad::midi::SessionCell, 256> launchedCells_;
    std::unique_ptr<juce::MidiOutput> virtualMidiOutput_;
    double sampleRate_{ 44100.0 };
    std::atomic<int> lastTrack_{ -1 };
    std::atomic<int> lastScene_{ -1 };
    std::atomic<int> launchSerial_{ 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GalahadMidiToolsProcessor)
};
