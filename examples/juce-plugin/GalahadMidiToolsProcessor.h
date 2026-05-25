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

    struct SurfaceMapSnapshot
    {
        int enabled{ 0 };
        int inputChannel{ 0 };
        int inputCc{ 0 };
        int outputChannel{ 0 };
        int outputCc{ 0 };
        int minimum{ 0 };
        int maximum{ 127 };
        int value{ 0 };
        int inputSet{ 0 };
        int valueSet{ 0 };

        bool hasInputAssignment() const noexcept { return inputSet != 0; }
        bool hasMapAssignment() const noexcept { return enabled != 0; }
        bool hasAnyAssignment() const noexcept { return hasInputAssignment() || hasMapAssignment(); }
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
    void syncAndLoadSurfaceEditorSelection();
    void loadSurfaceEditorFromMap(int controller, int control, int layer, int pattern, int targetChannel);
    SurfaceMapSnapshot surfaceMapSnapshot(int controller, int control, int layer, int pattern, int targetChannel) const noexcept;
    void setSurfaceMapSnapshot(int controller, int control, int layer, int pattern, int targetChannel, const SurfaceMapSnapshot& snapshot) noexcept;
    void learnSurfaceMap(int controller, int control, int layer, int pattern, int targetChannel, const ControllerSnapshot& snapshot) noexcept;
    void refreshHardwareMidiInputs();
    std::vector<juce::MidiDeviceInfo> availableMidiInputs() const;
    juce::StringArray activeHardwareInputNames() const;
    juce::StringArray controllerSlotDeviceNames() const;
    juce::String controllerSlotDeviceIdentifier(int slot) const;
    void setControllerSlotDeviceIdentifier(int slot, const juce::String& identifier);
    int activeHardwareInputCount() const noexcept { return activeHardwareInputCount_.load(std::memory_order_relaxed); }

private:
    static constexpr size_t MaxBlockEvents = galahad::MidiToolsEngine::MaxBlockEvents;
    static constexpr int ControllerMapSlotCount = galahad::plugin::ControllerMapSlotCount;
    static constexpr int ControllerSlotCount = galahad::plugin::ControllerSlotCount;
    static constexpr int ControllerLayerCount = galahad::plugin::ControllerLayerCount;
    static constexpr int ControllerPatternCount = galahad::plugin::ControllerPatternCount;
    static constexpr int ControllerTargetChannelCount = galahad::plugin::ControllerTargetChannelCount;
    static constexpr int ControllerSurfaceControlCount = galahad::plugin::ControllerSurfaceControlCount;
    static constexpr int ControllerSurfaceMapSlotCount = galahad::plugin::ControllerSurfaceMapSlotCount;

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

    struct ControllerSurfaceMap
    {
        std::atomic<int> enabled{ 0 };
        std::atomic<int> inputChannel{ 0 };
        std::atomic<int> inputCc{ 0 };
        std::atomic<int> outputChannel{ 0 };
        std::atomic<int> outputCc{ 0 };
        std::atomic<int> minimum{ 0 };
        std::atomic<int> maximum{ 127 };
        std::atomic<int> value{ 0 };
        std::atomic<int> inputSet{ 0 };
        std::atomic<int> valueSet{ 0 };
    };

    struct SurfaceEditorParameters
    {
        std::atomic<float>* controller{ nullptr };
        std::atomic<float>* control{ nullptr };
        std::atomic<float>* layer{ nullptr };
        std::atomic<float>* enabled{ nullptr };
        std::atomic<float>* inputChannel{ nullptr };
        std::atomic<float>* inputCc{ nullptr };
        std::atomic<float>* outputChannel{ nullptr };
        std::atomic<float>* outputCc{ nullptr };
        std::atomic<float>* minimum{ nullptr };
        std::atomic<float>* maximum{ nullptr };
    };

    struct SurfaceEditorSnapshot
    {
        int controller{ 0 };
        int control{ 0 };
        int layer{ 0 };
        int pattern{ 0 };
        int targetChannel{ 0 };
        int enabled{ 0 };
        int inputChannel{ 0 };
        int inputCc{ 0 };
        int outputChannel{ 0 };
        int outputCc{ 0 };
        int minimum{ 0 };
        int maximum{ 127 };
    };

    struct SurfacePerformanceContext
    {
        int layer{ 0 };
        int pattern{ 0 };
        int targetChannel{ 0 };
    };

    struct HardwareMidiInput
    {
        int slot{ 0 };
        juce::String name;
        std::unique_ptr<juce::MidiMessageCollector> collector;
        std::unique_ptr<juce::MidiInput> input;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static galahad::MidiEvent fromJuceMidi(const juce::MidiMessage& message, int sampleOffset) noexcept;
    static juce::MidiMessage toJuceMidi(const galahad::MidiEvent& event);

    void updateEngineConfig(double bpm);
    void ensureVirtualMidiOutput();
    void cacheControllerMapParameters();
    void initializeControllerSurfaceMaps() noexcept;
    void syncSurfaceEditorToSelectedMap() noexcept;
    void setSurfaceEditorParameter(const char* id, float value);
    SurfacePerformanceContext currentSurfacePerformanceContext() const noexcept;
    void appendSurfaceRecallEvents(const SurfacePerformanceContext& context,
                                   std::span<galahad::MidiEvent> output,
                                   size_t& outputCount) noexcept;
    juce::ValueTree createControllerSurfaceMapsState() const;
    void restoreControllerSurfaceMapsState(const juce::ValueTree& state);
    juce::ValueTree createControllerDeviceSlotsState() const;
    void restoreControllerDeviceSlotsState(const juce::ValueTree& state);
    void assignDefaultControllerSlotsIfNeeded(const std::vector<juce::MidiDeviceInfo>& devices);
    void closeHardwareMidiInputs();
    bool shouldCaptureHardwareInputs() const noexcept;
    static bool isPreferredHardwareInput(const juce::MidiDeviceInfo& device);
    bool appendControllerMappings(const galahad::MidiEvent& event,
                                  int sourceControllerSlot,
                                  std::span<galahad::MidiEvent> output,
                                  size_t& outputCount) noexcept;
    bool shouldPassMappedSource() const noexcept;
    void recordControllerInput(const galahad::MidiEvent& event) noexcept;
    void recordControllerOutput(const galahad::MidiEvent& event, int slot) noexcept;

    galahad::MidiToolsEngine engine_;
    juce::AudioProcessorValueTreeState parameters_;
    std::array<ControllerMapParameters, ControllerMapSlotCount> controllerMapParameters_{};
    SurfaceEditorParameters surfaceEditorParameters_{};
    SurfaceEditorSnapshot lastSurfaceEditorSnapshot_{};
    bool hasSurfaceEditorSnapshot_{ false };
    SurfacePerformanceContext lastSurfacePerformanceContext_{};
    bool hasSurfacePerformanceContext_{ false };
    std::array<ControllerSurfaceMap, ControllerSurfaceMapSlotCount> controllerSurfaceMaps_{};
    galahad::SpscQueue<galahad::midi::SessionCell, 256> launchedCells_;
    std::unique_ptr<juce::MidiOutput> virtualMidiOutput_;
    std::vector<HardwareMidiInput> hardwareMidiInputs_;
    juce::StringArray activeHardwareInputNames_;
    std::array<juce::String, ControllerSlotCount> controllerSlotDeviceIdentifiers_{};
    std::array<juce::String, ControllerSlotCount> controllerSlotDeviceNames_{};
    mutable std::mutex hardwareMidiInputsMutex_;
    double sampleRate_{ 44100.0 };
    bool controllerSlotAssignmentsManual_{ false };
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
