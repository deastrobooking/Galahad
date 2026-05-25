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
    class SurfaceRowsContent;
    class SurfaceRow;

    void timerCallback() override;
    void beginLearn(int slot);
    void beginSurfaceLearn(int control);
    void finishLearn(const GalahadMidiToolsProcessor::ControllerSnapshot& snapshot);
    void finishSurfaceLearn(const GalahadMidiToolsProcessor::ControllerSnapshot& snapshot);
    void refreshLearningState();
    void refreshSurfaceRows();
    void updateHardwareStatus();
    void updateSetupState();
    void updateSurfaceSummary();
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
    juce::Label surfaceLabel_;
    juce::Label surfaceSummaryLabel_;
    std::array<juce::TextButton, 8> controllerButtons_;
    std::array<juce::TextButton, 4> layerButtons_;
    std::array<std::unique_ptr<CircleButton>, 16> channelButtons_;
    std::array<std::unique_ptr<CircleButton>, 4> automationButtons_;
    juce::ComboBox surfaceController_;
    juce::ComboBox surfaceControl_;
    juce::ComboBox surfaceMap_;
    juce::ToggleButton surfaceEnabled_;
    juce::ComboBox surfaceInputChannel_;
    juce::Slider surfaceInputCc_;
    juce::ComboBox surfaceOutputChannel_;
    juce::Slider surfaceOutputCc_;
    juce::Slider surfaceMinimum_;
    juce::Slider surfaceMaximum_;
    juce::Viewport surfaceViewport_;
    std::unique_ptr<SurfaceRowsContent> surfaceRowsContent_;
    std::array<std::unique_ptr<SurfaceRow>, galahad::plugin::ControllerSurfaceControlCount> surfaceRows_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hardwareCaptureAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> thruAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> surfaceControllerAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> surfaceControlAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> surfaceMapAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> surfaceEnabledAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> surfaceInputChannelAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> surfaceInputCcAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> surfaceOutputChannelAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> surfaceOutputCcAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> surfaceMinimumAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> surfaceMaximumAttachment_;
    std::unique_ptr<ActivityView> activityView_;
    std::array<std::unique_ptr<MapRow>, galahad::plugin::ControllerMapSlotCount> rows_;
    int learningSlot_{ -1 };
    int surfaceLearningControl_{ -1 };
    int learnStartSerial_{ 0 };
    int surfaceLearnStartSerial_{ 0 };
    int lastInputSerial_{ 0 };
    int lastOutputSerial_{ 0 };
    int lastHardwareInputCount_{ -1 };
    int selectedControllerSlot_{ 0 };
    int selectedLayer_{ 0 };
    int selectedTargetChannel_{ 1 };
    int selectedAutomationSlot_{ 0 };
    int selectedSurfaceControl_{ 0 };
    int lastSurfaceController_{ -1 };
    int lastSurfaceControl_{ -1 };
    int lastSurfaceMap_{ -1 };
    int lastSurfacePattern_{ -1 };
    int lastSurfaceTargetChannel_{ -1 };
    bool lastHardwareCaptureState_{ true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GalahadMidiToolsEditor)
};
