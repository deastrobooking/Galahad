#pragma once

#include "GalahadMidiToolsParameters.h"

#include "galahad/MidiToolsEngine.h"
#include "galahad/SpscQueue.h"
#include "galahad/juce/MidiProtocol.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

class GalahadMidiToolsProcessor final : public juce::AudioProcessor
{
public:
    struct ControllerSnapshot
    {
        int channel{ 0 };
        int controller{ -1 };
        int value{ 0 };
        int slot{ -1 };
        int serial{ 0 };
    };

    GalahadMidiToolsProcessor();
    ~GalahadMidiToolsProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

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
    ControllerSnapshot lastControllerInput() const noexcept;
    ControllerSnapshot lastControllerOutput() const noexcept;
    juce::AudioProcessorValueTreeState& parameters() noexcept { return parameters_; }
    void refreshHardwareMidiInputs();
    juce::StringArray activeHardwareInputNames() const;
    int activeHardwareInputCount() const noexcept { return activeHardwareInputCount_.load(std::memory_order_relaxed); }

private:
    static constexpr size_t MaxBlockEvents = galahad::MidiToolsEngine::MaxBlockEvents;
    static constexpr int ControllerMapSlotCount = galahad::plugin::ControllerMapSlotCount;

    struct ControllerMapParameters
    {
        std::atomic<float>* enabled{ nullptr };
        std::atomic<float>* inputChannel{ nullptr };
        std::atomic<float>* inputCc{ nullptr };
        std::atomic<float>* outputChannel{ nullptr };
        std::atomic<float>* outputCc{ nullptr };
        std::atomic<float>* minimum{ nullptr };
        std::atomic<float>* maximum{ nullptr };
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static galahad::MidiEvent fromJuceMidi(const juce::MidiMessage& message, int sampleOffset) noexcept;
    static juce::MidiMessage toJuceMidi(const galahad::MidiEvent& event);

    void updateEngineConfig(double bpm);
    void ensureVirtualMidiOutput();
    void cacheControllerMapParameters();
    void closeHardwareMidiInputs();
    bool shouldCaptureHardwareInputs() const noexcept;
    static bool isPreferredHardwareInput(const juce::MidiDeviceInfo& device);
    bool appendControllerMappings(const galahad::MidiEvent& event,
                                  std::span<galahad::MidiEvent> output,
                                  size_t& outputCount) noexcept;
    bool shouldPassMappedSource() const noexcept;
    void recordControllerInput(const galahad::MidiEvent& event) noexcept;
    void recordControllerOutput(const galahad::MidiEvent& event, int slot) noexcept;

    galahad::MidiToolsEngine engine_;
    juce::AudioProcessorValueTreeState parameters_;
    std::array<ControllerMapParameters, ControllerMapSlotCount> controllerMapParameters_{};
    galahad::SpscQueue<galahad::midi::SessionCell, 256> launchedCells_;
    std::unique_ptr<juce::MidiOutput> virtualMidiOutput_;
    juce::MidiMessageCollector hardwareMidiCollector_;
    std::vector<std::unique_ptr<juce::MidiInput>> hardwareMidiInputs_;
    juce::StringArray activeHardwareInputNames_;
    mutable std::mutex hardwareMidiInputsMutex_;
    double sampleRate_{ 44100.0 };
    std::atomic<int> activeHardwareInputCount_{ 0 };
    std::atomic<int> lastTrack_{ -1 };
    std::atomic<int> lastScene_{ -1 };
    std::atomic<int> launchSerial_{ 0 };
    std::atomic<int> lastControllerInputChannel_{ 0 };
    std::atomic<int> lastControllerInputController_{ -1 };
    std::atomic<int> lastControllerInputValue_{ 0 };
    std::atomic<int> lastControllerInputSerial_{ 0 };
    std::atomic<int> lastControllerOutputChannel_{ 0 };
    std::atomic<int> lastControllerOutputController_{ -1 };
    std::atomic<int> lastControllerOutputValue_{ 0 };
    std::atomic<int> lastControllerOutputSlot_{ -1 };
    std::atomic<int> lastControllerOutputSerial_{ 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GalahadMidiToolsProcessor)
};
