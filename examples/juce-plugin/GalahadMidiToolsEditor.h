#pragma once

#include "GalahadMidiToolsProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

class GalahadMidiToolsEditor final : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit GalahadMidiToolsEditor(GalahadMidiToolsProcessor& processor);
    ~GalahadMidiToolsEditor() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    class ActivityView;
    class CircleButton;
    class MapRow;

    void timerCallback() override;
    void beginLearn(int slot);
    void finishLearn(const GalahadMidiToolsProcessor::ControllerSnapshot& snapshot);
    void refreshLearningState();
    void updateHardwareStatus();
    void updateSetupState();
    juce::String controllerSlotText(int slot) const;

    GalahadMidiToolsProcessor& processor_;
    juce::Label titleLabel_;
    juce::Label versionLabel_;
    juce::Label hardwareLabel_;
    juce::ToggleButton hardwareCaptureButton_;
    juce::TextButton rescanButton_;
    juce::ToggleButton thruButton_;
    juce::Label setupLabel_;
    juce::Label contextLabel_;
    std::array<juce::TextButton, 8> controllerButtons_;
    std::array<juce::TextButton, 4> layerButtons_;
    std::array<std::unique_ptr<CircleButton>, 16> channelButtons_;
    std::array<std::unique_ptr<CircleButton>, 4> automationButtons_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hardwareCaptureAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> thruAttachment_;
    std::unique_ptr<ActivityView> activityView_;
    std::array<std::unique_ptr<MapRow>, galahad::plugin::ControllerMapSlotCount> rows_;
    int learningSlot_{ -1 };
    int learnStartSerial_{ 0 };
    int lastInputSerial_{ 0 };
    int lastOutputSerial_{ 0 };
    int lastHardwareInputCount_{ -1 };
    int selectedControllerSlot_{ 0 };
    int selectedLayer_{ 0 };
    int selectedTargetChannel_{ 1 };
    int selectedAutomationSlot_{ 0 };
    bool lastHardwareCaptureState_{ true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GalahadMidiToolsEditor)
};
