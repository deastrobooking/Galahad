#include "GalahadMidiToolsEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int EditorWidth = 1120;
constexpr int EditorHeight = 1100;
constexpr int Margin = 18;
constexpr int RowHeight = 70;
constexpr int SurfaceRowHeight = 54;
constexpr int Gap = 10;
constexpr int SurfaceControlWidth = 96;
constexpr int SurfaceLayersWidth = 130;
constexpr int SurfaceMapWidth = 62;
constexpr int SurfaceLearnWidth = 72;
constexpr int SurfaceChannelWidth = 80;
constexpr int SurfaceCcWidth = 98;
constexpr int SurfaceRangeWidth = 88;

const juce::Colour Background{ 0xff11151c };
const juce::Colour Panel{ 0xff1a202a };
const juce::Colour PanelAlt{ 0xff202734 };
const juce::Colour Border{ 0xff343d4d };
const juce::Colour Text{ 0xffe8edf3 };
const juce::Colour MutedText{ 0xff9aa7b6 };
const juce::Colour Accent{ 0xff38bdf8 };
const juce::Colour AccentTwo{ 0xffffc857 };
const juce::Colour Good{ 0xff7bd88f };
const juce::Colour PinkRed{ 0xffff3d71 };

juce::String ccText(const GalahadMidiToolsProcessor::ControllerSnapshot& snapshot)
{
    if (snapshot.controller < 0)
        return "Ch --  CC --  000";

    return "Ch " + juce::String(snapshot.channel).paddedLeft('0', 2)
        + "  CC " + juce::String(snapshot.controller).paddedLeft('0', 3)
        + "  " + juce::String(snapshot.value).paddedLeft('0', 3);
}

void configureCombo(juce::ComboBox& combo)
{
    combo.setJustificationType(juce::Justification::centred);
    combo.setColour(juce::ComboBox::backgroundColourId, PanelAlt);
    combo.setColour(juce::ComboBox::outlineColourId, Border);
    combo.setColour(juce::ComboBox::textColourId, Text);
    combo.setColour(juce::ComboBox::arrowColourId, Accent);
}

void configureSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearBar);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 20);
    slider.setRange(0.0, 127.0, 1.0);
    slider.setColour(juce::Slider::trackColourId, Accent.withAlpha(0.45f));
    slider.setColour(juce::Slider::backgroundColourId, PanelAlt);
    slider.setColour(juce::Slider::textBoxTextColourId, Text);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, PanelAlt);
    slider.setColour(juce::Slider::textBoxOutlineColourId, Border);
}

void configurePanelButton(juce::TextButton& button, juce::Colour accent)
{
    button.setClickingTogglesState(false);
    button.setColour(juce::TextButton::buttonColourId, PanelAlt);
    button.setColour(juce::TextButton::buttonOnColourId, accent);
    button.setColour(juce::TextButton::textColourOffId, Text);
    button.setColour(juce::TextButton::textColourOnId, Background);
}

void setStatusButtonColours(juce::TextButton& button, bool active, bool assigned, juce::Colour accent)
{
    button.setColour(juce::TextButton::buttonColourId, active ? accent : (assigned ? accent.withAlpha(0.28f) : PanelAlt));
    button.setColour(juce::TextButton::buttonOnColourId, accent);
    button.setColour(juce::TextButton::textColourOffId, active ? Background : Text);
    button.setColour(juce::TextButton::textColourOnId, Background);
}

juce::Rectangle<int> takeColumn(juce::Rectangle<int>& area, int width)
{
    auto column = area.removeFromLeft(width);
    area.removeFromLeft(Gap);
    return column;
}

juce::String hardwareStatusText(const juce::StringArray& names)
{
    if (names.isEmpty())
        return "No controller inputs opened";

    return "Opened: " + names.joinIntoString(", ");
}

int parameterIntValue(const juce::AudioProcessorValueTreeState& parameters, const char* id)
{
    if (const auto* parameter = parameters.getRawParameterValue(id))
        return static_cast<int>(std::lround(parameter->load(std::memory_order_relaxed)));

    return 0;
}

void setChoiceParameter(juce::AudioProcessorValueTreeState& parameters, const char* id, int index)
{
    if (auto* parameter = parameters.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(index)));
}
} // namespace

class GalahadMidiToolsEditor::CircleButton final : public juce::Button
{
public:
    explicit CircleButton(const juce::String& text)
        : juce::Button(text)
    {
    }

    void setAccent(juce::Colour colour)
    {
        accent_ = colour;
        repaint();
    }

    void paintButton(juce::Graphics& graphics, bool isMouseOverButton, bool isButtonDown) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        const auto diameter = std::min(bounds.getWidth(), bounds.getHeight());
        auto circle = juce::Rectangle<float>(diameter, diameter).withCentre(bounds.getCentre());
        circle.reduce(isButtonDown ? 3.0f : 1.0f, isButtonDown ? 3.0f : 1.0f);

        const bool selected = getToggleState();
        graphics.setColour(selected ? accent_.withAlpha(0.28f) : PanelAlt);
        graphics.fillEllipse(circle);
        graphics.setColour(selected ? accent_ : (isMouseOverButton ? accent_.withAlpha(0.85f) : Border));
        graphics.drawEllipse(circle, selected ? 2.0f : 1.0f);

        auto inner = circle.reduced(4.0f).toNearestInt();
        graphics.setColour(selected ? Text : MutedText);
        graphics.setFont(juce::FontOptions(diameter > 46.0f ? 15.0f : 13.0f,
                                           selected ? juce::Font::bold : juce::Font::plain));
        graphics.drawText(getButtonText(), inner, juce::Justification::centred);
    }

private:
    juce::Colour accent_{ Accent };
};

class GalahadMidiToolsEditor::ActivityView final : public juce::Component
{
public:
    void setInput(const GalahadMidiToolsProcessor::ControllerSnapshot& snapshot)
    {
        input_ = snapshot;
        inputPulse_ = 1.0f;
        repaint();
    }

    void setOutput(const GalahadMidiToolsProcessor::ControllerSnapshot& snapshot)
    {
        output_ = snapshot;
        outputPulse_ = 1.0f;
        repaint();
    }

    void decay()
    {
        inputPulse_ = std::max(0.0f, inputPulse_ - 0.06f);
        outputPulse_ = std::max(0.0f, outputPulse_ - 0.06f);
        repaint();
    }

    void paint(juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds().toFloat();
        graphics.setColour(Panel);
        graphics.fillRoundedRectangle(bounds, 8.0f);
        graphics.setColour(Border);
        graphics.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

        auto area = getLocalBounds().reduced(18, 14);
        drawLane(graphics, area.removeFromLeft(area.getWidth() / 2 - 8), "Input", ccText(input_), input_.value, Accent, inputPulse_);
        area.removeFromLeft(16);
        const auto outputText = output_.slot >= 0
            ? "S" + juce::String(output_.slot + 1) + "  " + ccText(output_)
            : ccText(output_);
        drawLane(graphics, area, "Output", outputText, output_.value, AccentTwo, outputPulse_);
    }

private:
    static void drawLane(juce::Graphics& graphics,
                         juce::Rectangle<int> area,
                         const juce::String& label,
                         const juce::String& valueText,
                         int value,
                         juce::Colour colour,
                         float pulse)
    {
        graphics.setColour(MutedText);
        graphics.setFont(13.0f);
        graphics.drawText(label, area.removeFromTop(18), juce::Justification::centredLeft);

        graphics.setColour(Text);
        graphics.setFont(24.0f);
        graphics.drawText(valueText, area.removeFromTop(34), juce::Justification::centredLeft);

        auto meter = area.removeFromTop(10).reduced(0, 2).toFloat();
        graphics.setColour(PanelAlt);
        graphics.fillRoundedRectangle(meter, 4.0f);

        const float width = meter.getWidth() * juce::jlimit(0.0f, 1.0f, static_cast<float>(value) / 127.0f);
        auto active = meter.withWidth(width);
        graphics.setColour(colour.withAlpha(0.55f + 0.35f * pulse));
        graphics.fillRoundedRectangle(active, 4.0f);
    }

    GalahadMidiToolsProcessor::ControllerSnapshot input_{};
    GalahadMidiToolsProcessor::ControllerSnapshot output_{};
    float inputPulse_{ 0.0f };
    float outputPulse_{ 0.0f };
};

class GalahadMidiToolsEditor::MapRow final : public juce::Component
{
public:
    MapRow(GalahadMidiToolsEditor& owner, GalahadMidiToolsProcessor& processor, int slot)
        : owner_(owner),
          slot_(slot)
    {
        slotLabel_.setText(juce::String(slot + 1), juce::dontSendNotification);
        slotLabel_.setJustificationType(juce::Justification::centred);
        slotLabel_.setColour(juce::Label::textColourId, Text);
        addAndMakeVisible(slotLabel_);

        enabledButton_.setButtonText("On");
        enabledButton_.setColour(juce::ToggleButton::textColourId, Text);
        addAndMakeVisible(enabledButton_);

        learnButton_.setButtonText("Learn");
        learnButton_.setColour(juce::TextButton::buttonColourId, PanelAlt);
        learnButton_.setColour(juce::TextButton::buttonOnColourId, Accent);
        learnButton_.setColour(juce::TextButton::textColourOffId, Text);
        learnButton_.onClick = [this] { owner_.beginLearn(slot_); };
        addAndMakeVisible(learnButton_);

        inputChannel_.addItemList(galahad::plugin::inputChannelChoices(), 1);
        outputChannel_.addItemList(galahad::plugin::outputChannelChoices(), 1);
        configureCombo(inputChannel_);
        configureCombo(outputChannel_);
        addAndMakeVisible(inputChannel_);
        addAndMakeVisible(outputChannel_);

        configureSlider(inputCc_);
        configureSlider(outputCc_);
        configureSlider(minimum_);
        configureSlider(maximum_);
        addAndMakeVisible(inputCc_);
        addAndMakeVisible(outputCc_);
        addAndMakeVisible(minimum_);
        addAndMakeVisible(maximum_);

        auto& parameters = processor.parameters();
        enabledAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            parameters,
            galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapEnabledSuffix),
            enabledButton_);
        inputChannelAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            parameters,
            galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapInputChannelSuffix),
            inputChannel_);
        inputCcAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters,
            galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapInputCcSuffix),
            inputCc_);
        outputChannelAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            parameters,
            galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapOutputChannelSuffix),
            outputChannel_);
        outputCcAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters,
            galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapOutputCcSuffix),
            outputCc_);
        minimumAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters,
            galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapMinimumSuffix),
            minimum_);
        maximumAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            parameters,
            galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapMaximumSuffix),
            maximum_);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12, 12);
        slotLabel_.setBounds(takeColumn(area, 34));
        enabledButton_.setBounds(takeColumn(area, 54));
        learnButton_.setBounds(takeColumn(area, 70));
        inputChannel_.setBounds(takeColumn(area, 88));
        inputCc_.setBounds(takeColumn(area, 94));
        outputChannel_.setBounds(takeColumn(area, 88));
        outputCc_.setBounds(takeColumn(area, 94));
        minimum_.setBounds(takeColumn(area, 94));
        maximum_.setBounds(takeColumn(area, 94));
    }

    void paint(juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds().toFloat();
        graphics.setColour(slot_ % 2 == 0 ? Panel : PanelAlt);
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(isLearning_ ? Accent : Border);
        graphics.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, isLearning_ ? 2.0f : 1.0f);

        auto activityBounds = getLocalBounds().removeFromRight(18).reduced(6, 12).toFloat();
        graphics.setColour(PanelAlt.darker(0.3f));
        graphics.fillRoundedRectangle(activityBounds, 3.0f);
        graphics.setColour(Good.withAlpha(0.35f + 0.55f * activityLevel_));
        graphics.fillRoundedRectangle(activityBounds.removeFromBottom(activityBounds.getHeight() * activityLevel_), 3.0f);
    }

    void setLearning(bool shouldLearn)
    {
        isLearning_ = shouldLearn;
        learnButton_.setButtonText(isLearning_ ? "Listening" : "Learn");
        repaint();
    }

    void learnFrom(const GalahadMidiToolsProcessor::ControllerSnapshot& snapshot)
    {
        enabledButton_.setToggleState(true, juce::sendNotificationSync);
        inputChannel_.setSelectedId(snapshot.channel + 1, juce::sendNotificationSync);
        inputCc_.setValue(snapshot.controller, juce::sendNotificationSync);
    }

    void pulse(int value)
    {
        activityLevel_ = juce::jlimit(0.0f, 1.0f, static_cast<float>(value) / 127.0f);
        activityHold_ = 1.0f;
        repaint();
    }

    void decay()
    {
        activityHold_ = std::max(0.0f, activityHold_ - 0.045f);
        activityLevel_ *= 0.95f;
        if (activityHold_ > 0.0f)
            activityLevel_ = std::max(activityLevel_, 0.18f);
        repaint();
    }

private:
    GalahadMidiToolsEditor& owner_;
    int slot_{ 0 };
    juce::Label slotLabel_;
    juce::ToggleButton enabledButton_;
    juce::TextButton learnButton_;
    juce::ComboBox inputChannel_;
    juce::Slider inputCc_;
    juce::ComboBox outputChannel_;
    juce::Slider outputCc_;
    juce::Slider minimum_;
    juce::Slider maximum_;
    bool isLearning_{ false };
    float activityLevel_{ 0.0f };
    float activityHold_{ 0.0f };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> inputChannelAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputCcAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> outputChannelAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputCcAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> minimumAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> maximumAttachment_;
};

class GalahadMidiToolsEditor::SurfaceRow final : public juce::Component
{
public:
    SurfaceRow(GalahadMidiToolsEditor& owner, GalahadMidiToolsProcessor& processor, int control)
        : owner_(owner),
          processor_(processor),
          control_(control)
    {
        controlLabel_.setText(galahad::plugin::controllerSurfaceControlName(control), juce::dontSendNotification);
        controlLabel_.setJustificationType(juce::Justification::centredLeft);
        controlLabel_.setColour(juce::Label::textColourId, Text);
        addAndMakeVisible(controlLabel_);

        for (int layer = 0; layer < static_cast<int>(layerButtons_.size()); ++layer)
        {
            auto& button = layerButtons_[static_cast<size_t>(layer)];
            button.setButtonText(juce::String(layer + 1));
            configurePanelButton(button, PinkRed);
            button.onClick = [this, layer] {
                owner_.selectedLayer_ = layer;
                owner_.selectedSurfaceControl_ = control_;
                setChoiceParameter(processor_.parameters(), galahad::plugin::ControllerLayerId, layer);
                setChoiceParameter(processor_.parameters(), galahad::plugin::SurfaceEditLayerId, layer);
                selectForDetail();
                owner_.refreshSurfaceRows();
                owner_.updateSetupState();
            };
            addAndMakeVisible(button);
        }

        mapButton_.setButtonText("Map");
        configurePanelButton(mapButton_, Good);
        mapButton_.onClick = [this] {
            auto snapshot = currentSnapshot();
            snapshot.enabled = snapshot.enabled == 0 ? 1 : 0;
            processor_.setSurfaceMapSnapshot(controller(), control_, layer(), pattern(), targetChannel(), snapshot);
            selectForDetail();
            refresh();
            owner_.updateSurfaceSummary();
        };
        addAndMakeVisible(mapButton_);

        learnButton_.setButtonText("Learn");
        configurePanelButton(learnButton_, Accent);
        learnButton_.onClick = [this] {
            owner_.beginSurfaceLearn(control_);
        };
        addAndMakeVisible(learnButton_);

        inputChannel_.addItemList(galahad::plugin::inputChannelChoices(), 1);
        outputChannel_.addItemList(galahad::plugin::outputChannelChoices(), 1);
        configureCombo(inputChannel_);
        configureCombo(outputChannel_);
        addAndMakeVisible(inputChannel_);
        addAndMakeVisible(outputChannel_);

        configureSlider(inputCc_);
        configureSlider(outputCc_);
        configureSlider(minimum_);
        configureSlider(maximum_);
        addAndMakeVisible(inputCc_);
        addAndMakeVisible(outputCc_);
        addAndMakeVisible(minimum_);
        addAndMakeVisible(maximum_);

        inputChannel_.onChange = [this] {
            if (refreshing_)
                return;
            auto snapshot = currentSnapshot();
            snapshot.inputChannel = juce::jlimit(0, 16, inputChannel_.getSelectedId() - 1);
            snapshot.inputSet = 1;
            writeSnapshot(snapshot);
        };
        inputCc_.onValueChange = [this] {
            if (refreshing_)
                return;
            auto snapshot = currentSnapshot();
            snapshot.inputCc = juce::jlimit(0, 127, static_cast<int>(std::lround(inputCc_.getValue())));
            snapshot.inputSet = 1;
            writeSnapshot(snapshot);
        };
        outputChannel_.onChange = [this] {
            if (refreshing_)
                return;
            auto snapshot = currentSnapshot();
            snapshot.outputChannel = juce::jlimit(0, 15, outputChannel_.getSelectedId() - 1);
            writeSnapshot(snapshot);
        };
        outputCc_.onValueChange = [this] {
            if (refreshing_)
                return;
            auto snapshot = currentSnapshot();
            snapshot.outputCc = juce::jlimit(0, 127, static_cast<int>(std::lround(outputCc_.getValue())));
            writeSnapshot(snapshot);
        };
        minimum_.onValueChange = [this] {
            if (refreshing_)
                return;
            auto snapshot = currentSnapshot();
            snapshot.minimum = juce::jlimit(0, 127, static_cast<int>(std::lround(minimum_.getValue())));
            writeSnapshot(snapshot);
        };
        maximum_.onValueChange = [this] {
            if (refreshing_)
                return;
            auto snapshot = currentSnapshot();
            snapshot.maximum = juce::jlimit(0, 127, static_cast<int>(std::lround(maximum_.getValue())));
            writeSnapshot(snapshot);
        };
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10, 8);
        controlLabel_.setBounds(takeColumn(area, SurfaceControlWidth));

        auto layers = takeColumn(area, SurfaceLayersWidth);
        for (auto& button : layerButtons_)
            button.setBounds(layers.removeFromLeft(28).reduced(2, 4));

        mapButton_.setBounds(takeColumn(area, SurfaceMapWidth).reduced(0, 4));
        learnButton_.setBounds(takeColumn(area, SurfaceLearnWidth).reduced(0, 4));
        inputChannel_.setBounds(takeColumn(area, SurfaceChannelWidth).reduced(0, 4));
        inputCc_.setBounds(takeColumn(area, SurfaceCcWidth).reduced(0, 4));
        outputChannel_.setBounds(takeColumn(area, SurfaceChannelWidth).reduced(0, 4));
        outputCc_.setBounds(takeColumn(area, SurfaceCcWidth).reduced(0, 4));
        minimum_.setBounds(takeColumn(area, SurfaceRangeWidth).reduced(0, 4));
        maximum_.setBounds(takeColumn(area, SurfaceRangeWidth).reduced(0, 4));
    }

    void paint(juce::Graphics& graphics) override
    {
        const bool selected = owner_.selectedSurfaceControl_ == control_;
        auto bounds = getLocalBounds().toFloat().reduced(0.0f, 2.0f);
        graphics.setColour(control_ % 2 == 0 ? Panel : PanelAlt);
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(isLearning_ ? Accent : (selected ? AccentTwo : Border));
        graphics.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, selected || isLearning_ ? 2.0f : 1.0f);
    }

    void refresh()
    {
        refreshing_ = true;

        const auto active = currentSnapshot();
        controlLabel_.setColour(juce::Label::textColourId, active.hasAnyAssignment() ? Text : MutedText);
        for (int layerIndex = 0; layerIndex < static_cast<int>(layerButtons_.size()); ++layerIndex)
        {
            const auto layerSnapshot = processor_.surfaceMapSnapshot(controller(), control_, layerIndex, pattern(), targetChannel());
            const bool selected = layerIndex == layer();
            auto& button = layerButtons_[static_cast<size_t>(layerIndex)];
            button.setToggleState(selected, juce::dontSendNotification);
            setStatusButtonColours(button, selected, layerSnapshot.hasAnyAssignment(), selected ? PinkRed : AccentTwo);
        }

        mapButton_.setToggleState(active.enabled != 0, juce::dontSendNotification);
        setStatusButtonColours(mapButton_, active.enabled != 0, active.hasMapAssignment(), Good);
        setStatusButtonColours(learnButton_, isLearning_, active.hasInputAssignment(), Accent);
        learnButton_.setButtonText(isLearning_ ? "Listening" : "Learn");

        inputChannel_.setSelectedId(juce::jlimit(1, 17, active.inputChannel + 1), juce::dontSendNotification);
        inputCc_.setValue(active.inputCc, juce::dontSendNotification);
        outputChannel_.setSelectedId(juce::jlimit(1, 16, active.outputChannel + 1), juce::dontSendNotification);
        outputCc_.setValue(active.outputCc, juce::dontSendNotification);
        minimum_.setValue(active.minimum, juce::dontSendNotification);
        maximum_.setValue(active.maximum, juce::dontSendNotification);

        refreshing_ = false;
        repaint();
    }

    void setLearning(bool shouldLearn)
    {
        isLearning_ = shouldLearn;
        refresh();
    }

    void learnFrom(const GalahadMidiToolsProcessor::ControllerSnapshot& snapshot)
    {
        processor_.learnSurfaceMap(controller(), control_, layer(), pattern(), targetChannel(), snapshot);
        owner_.selectedSurfaceControl_ = control_;
        selectForDetail();
        refresh();
    }

private:
    int controller() const noexcept { return owner_.selectedControllerSlot_; }
    int layer() const noexcept { return owner_.selectedLayer_; }
    int pattern() const noexcept { return owner_.selectedAutomationSlot_; }
    int targetChannel() const noexcept { return juce::jlimit(0, galahad::plugin::ControllerTargetChannelCount - 1, owner_.selectedTargetChannel_ - 1); }

    GalahadMidiToolsProcessor::SurfaceMapSnapshot currentSnapshot() const noexcept
    {
        return processor_.surfaceMapSnapshot(controller(), control_, layer(), pattern(), targetChannel());
    }

    void writeSnapshot(const GalahadMidiToolsProcessor::SurfaceMapSnapshot& snapshot)
    {
        processor_.setSurfaceMapSnapshot(controller(), control_, layer(), pattern(), targetChannel(), snapshot);
        owner_.selectedSurfaceControl_ = control_;
        selectForDetail();
        refresh();
        owner_.updateSurfaceSummary();
    }

    void selectForDetail()
    {
        setChoiceParameter(processor_.parameters(), galahad::plugin::SurfaceEditControllerId, controller());
        setChoiceParameter(processor_.parameters(), galahad::plugin::SurfaceEditControlId, control_);
        setChoiceParameter(processor_.parameters(), galahad::plugin::SurfaceEditLayerId, layer());
        processor_.loadSurfaceEditorFromMap(controller(), control_, layer(), pattern(), targetChannel());
    }

    GalahadMidiToolsEditor& owner_;
    GalahadMidiToolsProcessor& processor_;
    int control_{ 0 };
    juce::Label controlLabel_;
    std::array<juce::TextButton, galahad::plugin::ControllerLayerCount> layerButtons_;
    juce::TextButton mapButton_;
    juce::TextButton learnButton_;
    juce::ComboBox inputChannel_;
    juce::Slider inputCc_;
    juce::ComboBox outputChannel_;
    juce::Slider outputCc_;
    juce::Slider minimum_;
    juce::Slider maximum_;
    bool refreshing_{ false };
    bool isLearning_{ false };
};

class GalahadMidiToolsEditor::SurfaceRowsContent final : public juce::Component
{
public:
    explicit SurfaceRowsContent(GalahadMidiToolsEditor& owner)
        : owner_(owner)
    {
    }

    void resized() override
    {
        auto area = getLocalBounds();
        for (auto& row : owner_.surfaceRows_)
        {
            if (row != nullptr)
                row->setBounds(area.removeFromTop(SurfaceRowHeight));
        }
    }

private:
    GalahadMidiToolsEditor& owner_;
};

GalahadMidiToolsEditor::GalahadMidiToolsEditor(GalahadMidiToolsProcessor& audioProcessor)
    : juce::AudioProcessorEditor(audioProcessor),
      processor_(audioProcessor)
{
    titleLabel_.setText("Galahad Mapper", juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    titleLabel_.setColour(juce::Label::textColourId, Text);
    titleLabel_.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel_);

    versionLabel_.setText("MIDI Controller Layers", juce::dontSendNotification);
    versionLabel_.setJustificationType(juce::Justification::centredLeft);
    versionLabel_.setColour(juce::Label::textColourId, MutedText);
    versionLabel_.setFont(juce::FontOptions(14.0f));
    addAndMakeVisible(versionLabel_);

    hardwareLabel_.setText(hardwareStatusText(processor_.activeHardwareInputNames()), juce::dontSendNotification);
    hardwareLabel_.setJustificationType(juce::Justification::centredLeft);
    hardwareLabel_.setColour(juce::Label::textColourId, MutedText);
    hardwareLabel_.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(hardwareLabel_);

    deviceLabel_.setText("MIDI Inputs", juce::dontSendNotification);
    deviceLabel_.setJustificationType(juce::Justification::centredLeft);
    deviceLabel_.setColour(juce::Label::textColourId, MutedText);
    deviceLabel_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    addAndMakeVisible(deviceLabel_);

    setupLabel_.setText("Setup", juce::dontSendNotification);
    setupLabel_.setJustificationType(juce::Justification::centredLeft);
    setupLabel_.setColour(juce::Label::textColourId, Text);
    setupLabel_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    addAndMakeVisible(setupLabel_);

    contextLabel_.setJustificationType(juce::Justification::centredRight);
    contextLabel_.setColour(juce::Label::textColourId, MutedText);
    contextLabel_.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(contextLabel_);

    surfaceLabel_.setText("Surface Assignment", juce::dontSendNotification);
    surfaceLabel_.setJustificationType(juce::Justification::centredLeft);
    surfaceLabel_.setColour(juce::Label::textColourId, Text);
    surfaceLabel_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    addAndMakeVisible(surfaceLabel_);

    surfaceSummaryLabel_.setJustificationType(juce::Justification::centredRight);
    surfaceSummaryLabel_.setColour(juce::Label::textColourId, MutedText);
    surfaceSummaryLabel_.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(surfaceSummaryLabel_);

    for (int slot = 0; slot < static_cast<int>(controllerButtons_.size()); ++slot)
    {
        auto& button = controllerButtons_[static_cast<size_t>(slot)];
        configurePanelButton(button, Accent);
        button.onClick = [this, slot] {
            selectedControllerSlot_ = slot;
            setChoiceParameter(processor_.parameters(), galahad::plugin::SurfaceEditControllerId, slot);
            refreshSurfaceRows();
            updateSetupState();
        };
        addAndMakeVisible(button);
    }

    for (int slot = 0; slot < static_cast<int>(deviceSelectors_.size()); ++slot)
    {
        auto& selector = deviceSelectors_[static_cast<size_t>(slot)];
        configureCombo(selector);
        selector.onChange = [this, slot] {
            if (refreshingDeviceSelectors_)
                return;

            const int selectedId = deviceSelectors_[static_cast<size_t>(slot)].getSelectedId();
            juce::String identifier;
            if (selectedId >= 2 && selectedId < 2 + deviceSelectorIdentifiers_.size())
                identifier = deviceSelectorIdentifiers_[selectedId - 2];
            else if (selectedId >= 1000)
                identifier = processor_.controllerSlotDeviceIdentifier(slot);

            processor_.setControllerSlotDeviceIdentifier(slot, identifier);
            refreshDeviceSelectors();
            updateHardwareStatus();
            refreshSurfaceRows();
            lastHardwareInputCount_ = processor_.activeHardwareInputCount();
        };
        addAndMakeVisible(selector);
    }

    for (int layer = 0; layer < static_cast<int>(layerButtons_.size()); ++layer)
    {
        auto& button = layerButtons_[static_cast<size_t>(layer)];
        button.setButtonText(juce::String::charToString(static_cast<juce::juce_wchar>('A' + layer)));
        configurePanelButton(button, PinkRed);
        button.onClick = [this, layer] {
            selectedLayer_ = layer;
            setChoiceParameter(processor_.parameters(), galahad::plugin::ControllerLayerId, layer);
            setChoiceParameter(processor_.parameters(), galahad::plugin::SurfaceEditLayerId, layer);
            refreshSurfaceRows();
            updateSetupState();
        };
        addAndMakeVisible(button);
    }

    for (int channel = 0; channel < static_cast<int>(channelButtons_.size()); ++channel)
    {
        auto button = std::make_unique<CircleButton>(juce::String(channel + 1));
        button->setAccent(channel < 8 ? Accent : Good);
        button->onClick = [this, channel] {
            selectedTargetChannel_ = channel + 1;
            setChoiceParameter(processor_.parameters(), galahad::plugin::ControllerTargetChannelId, channel);
            refreshSurfaceRows();
            updateSetupState();
        };
        addAndMakeVisible(*button);
        channelButtons_[static_cast<size_t>(channel)] = std::move(button);
    }

    for (int clip = 0; clip < static_cast<int>(automationButtons_.size()); ++clip)
    {
        auto button = std::make_unique<CircleButton>("C" + juce::String(clip + 1));
        button->setAccent(clip % 2 == 0 ? AccentTwo : Good);
        button->onClick = [this, clip] {
            selectedAutomationSlot_ = clip;
            setChoiceParameter(processor_.parameters(), galahad::plugin::ControllerPatternId, clip);
            refreshSurfaceRows();
            updateSetupState();
        };
        addAndMakeVisible(*button);
        automationButtons_[static_cast<size_t>(clip)] = std::move(button);
    }

    surfaceController_.addItemList(galahad::plugin::controllerSlotChoices(), 1);
    surfaceControl_.addItemList(galahad::plugin::controllerSurfaceControlChoices(), 1);
    surfaceMap_.addItemList(galahad::plugin::controllerLayerChoices(), 1);
    surfaceInputChannel_.addItemList(galahad::plugin::inputChannelChoices(), 1);
    surfaceOutputChannel_.addItemList(galahad::plugin::outputChannelChoices(), 1);
    configureCombo(surfaceController_);
    configureCombo(surfaceControl_);
    configureCombo(surfaceMap_);
    configureCombo(surfaceInputChannel_);
    configureCombo(surfaceOutputChannel_);
    addAndMakeVisible(surfaceController_);
    addAndMakeVisible(surfaceControl_);
    addAndMakeVisible(surfaceMap_);
    addAndMakeVisible(surfaceInputChannel_);
    addAndMakeVisible(surfaceOutputChannel_);

    surfaceEnabled_.setButtonText("On");
    surfaceEnabled_.setColour(juce::ToggleButton::textColourId, Text);
    addAndMakeVisible(surfaceEnabled_);

    configureSlider(surfaceInputCc_);
    configureSlider(surfaceOutputCc_);
    configureSlider(surfaceMinimum_);
    configureSlider(surfaceMaximum_);
    addAndMakeVisible(surfaceInputCc_);
    addAndMakeVisible(surfaceOutputCc_);
    addAndMakeVisible(surfaceMinimum_);
    addAndMakeVisible(surfaceMaximum_);
    surfaceController_.setVisible(false);
    surfaceControl_.setVisible(false);
    surfaceMap_.setVisible(false);
    surfaceEnabled_.setVisible(false);
    surfaceInputChannel_.setVisible(false);
    surfaceInputCc_.setVisible(false);
    surfaceOutputChannel_.setVisible(false);
    surfaceOutputCc_.setVisible(false);
    surfaceMinimum_.setVisible(false);
    surfaceMaximum_.setVisible(false);

    surfaceRowsContent_ = std::make_unique<SurfaceRowsContent>(*this);
    surfaceViewport_.setScrollBarsShown(true, false);
    surfaceViewport_.setViewedComponent(surfaceRowsContent_.get(), false);
    addAndMakeVisible(surfaceViewport_);
    for (int control = 0; control < static_cast<int>(surfaceRows_.size()); ++control)
    {
        surfaceRows_[static_cast<size_t>(control)] = std::make_unique<SurfaceRow>(*this, processor_, control);
        surfaceRowsContent_->addAndMakeVisible(*surfaceRows_[static_cast<size_t>(control)]);
    }

    hardwareCaptureButton_.setButtonText("Hardware");
    hardwareCaptureButton_.setColour(juce::ToggleButton::textColourId, Text);
    addAndMakeVisible(hardwareCaptureButton_);
    hardwareCaptureAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor_.parameters(),
        galahad::plugin::HardwareCaptureId,
        hardwareCaptureButton_);

    rescanButton_.setButtonText("Rescan");
    rescanButton_.setColour(juce::TextButton::buttonColourId, PanelAlt);
    rescanButton_.setColour(juce::TextButton::textColourOffId, Text);
    rescanButton_.onClick = [this] {
        processor_.refreshHardwareMidiInputs();
        refreshDeviceSelectors();
        updateHardwareStatus();
        refreshSurfaceRows();
        lastHardwareInputCount_ = processor_.activeHardwareInputCount();
    };
    addAndMakeVisible(rescanButton_);

    thruButton_.setButtonText("Map Thru");
    thruButton_.setColour(juce::ToggleButton::textColourId, Text);
    addAndMakeVisible(thruButton_);
    thruAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor_.parameters(),
        galahad::plugin::MapThruId,
        thruButton_);

    auto& parameters = processor_.parameters();
    surfaceControllerAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        parameters,
        galahad::plugin::SurfaceEditControllerId,
        surfaceController_);
    surfaceControlAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        parameters,
        galahad::plugin::SurfaceEditControlId,
        surfaceControl_);
    surfaceMapAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        parameters,
        galahad::plugin::SurfaceEditLayerId,
        surfaceMap_);
    surfaceEnabledAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        parameters,
        galahad::plugin::SurfaceEditEnabledId,
        surfaceEnabled_);
    surfaceInputChannelAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        parameters,
        galahad::plugin::SurfaceEditInputChannelId,
        surfaceInputChannel_);
    surfaceInputCcAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        parameters,
        galahad::plugin::SurfaceEditInputCcId,
        surfaceInputCc_);
    surfaceOutputChannelAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        parameters,
        galahad::plugin::SurfaceEditOutputChannelId,
        surfaceOutputChannel_);
    surfaceOutputCcAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        parameters,
        galahad::plugin::SurfaceEditOutputCcId,
        surfaceOutputCc_);
    surfaceMinimumAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        parameters,
        galahad::plugin::SurfaceEditMinimumId,
        surfaceMinimum_);
    surfaceMaximumAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        parameters,
        galahad::plugin::SurfaceEditMaximumId,
        surfaceMaximum_);

    activityView_ = std::make_unique<ActivityView>();
    addAndMakeVisible(*activityView_);

    for (int slot = 0; slot < static_cast<int>(rows_.size()); ++slot)
    {
        rows_[static_cast<size_t>(slot)] = std::make_unique<MapRow>(*this, processor_, slot);
        addAndMakeVisible(*rows_[static_cast<size_t>(slot)]);
    }

    processor_.refreshHardwareMidiInputs();
    processor_.syncAndLoadSurfaceEditorSelection();
    refreshDeviceSelectors();
    updateHardwareStatus();
    updateSetupState();
    refreshSurfaceRows();
    updateSurfaceSummary();
    lastHardwareInputCount_ = processor_.activeHardwareInputCount();
    lastHardwareCaptureState_ = hardwareCaptureButton_.getToggleState();

    setSize(EditorWidth, EditorHeight);
    startTimerHz(30);
}

GalahadMidiToolsEditor::~GalahadMidiToolsEditor() = default;

void GalahadMidiToolsEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(Background);

    auto area = getLocalBounds().reduced(Margin);
    area.removeFromTop(690);

    graphics.setColour(MutedText);
    graphics.setFont(12.0f);
    auto header = area.removeFromTop(22).reduced(12, 0);
    graphics.drawText("#", takeColumn(header, 34), juce::Justification::centred);
    graphics.drawText("State", takeColumn(header, 54), juce::Justification::centredLeft);
    graphics.drawText("Learn", takeColumn(header, 70), juce::Justification::centredLeft);
    graphics.drawText("Input", takeColumn(header, 88), juce::Justification::centredLeft);
    graphics.drawText("CC", takeColumn(header, 94), juce::Justification::centredLeft);
    graphics.drawText("Output", takeColumn(header, 88), juce::Justification::centredLeft);
    graphics.drawText("CC", takeColumn(header, 94), juce::Justification::centredLeft);
    graphics.drawText("Min", takeColumn(header, 94), juce::Justification::centredLeft);
    graphics.drawText("Max", takeColumn(header, 94), juce::Justification::centredLeft);

    auto setup = getLocalBounds().reduced(Margin);
    setup.removeFromTop(78);
    setup.removeFromTop(42);
    setup.removeFromTop(40);
    setup.removeFromTop(8);
    auto lower = setup.removeFromTop(140);
    auto clips = lower.removeFromLeft(170);
    lower.removeFromLeft(18);
    auto layers = lower.removeFromLeft(235);
    lower.removeFromLeft(18);

    graphics.setColour(MutedText);
    graphics.setFont(12.0f);
    graphics.drawText("Automation Clips", clips.removeFromTop(18), juce::Justification::centredLeft);
    graphics.drawText("Performance Layers", layers.removeFromTop(18), juce::Justification::centredLeft);
    graphics.drawText("Target Channel", lower.removeFromTop(18), juce::Justification::centredLeft);

    auto surface = getLocalBounds().reduced(Margin);
    surface.removeFromTop(48 + 22 + 8 + 42 + 40 + 8 + 140);
    surface.removeFromTop(28);
    auto surfaceHeader = surface.removeFromTop(18);
    graphics.drawText("Control", takeColumn(surfaceHeader, SurfaceControlWidth), juce::Justification::centredLeft);
    graphics.drawText("Maps", takeColumn(surfaceHeader, SurfaceLayersWidth), juce::Justification::centredLeft);
    graphics.drawText("Map", takeColumn(surfaceHeader, SurfaceMapWidth), juce::Justification::centredLeft);
    graphics.drawText("Learn", takeColumn(surfaceHeader, SurfaceLearnWidth), juce::Justification::centredLeft);
    graphics.drawText("Input", takeColumn(surfaceHeader, SurfaceChannelWidth + SurfaceCcWidth + Gap), juce::Justification::centredLeft);
    graphics.drawText("Output", takeColumn(surfaceHeader, SurfaceChannelWidth + SurfaceCcWidth + Gap), juce::Justification::centredLeft);
    graphics.drawText("Range", surfaceHeader, juce::Justification::centredLeft);
}

void GalahadMidiToolsEditor::resized()
{
    auto area = getLocalBounds().reduced(Margin);
    auto top = area.removeFromTop(48);
    titleLabel_.setBounds(top.removeFromLeft(260));
    versionLabel_.setBounds(top.removeFromLeft(220));
    rescanButton_.setBounds(top.removeFromRight(86).reduced(0, 7));
    hardwareCaptureButton_.setBounds(top.removeFromRight(118).reduced(0, 7));
    thruButton_.setBounds(top.removeFromRight(118).reduced(0, 7));

    hardwareLabel_.setBounds(area.removeFromTop(22));
    area.removeFromTop(8);

    auto controllerRow = area.removeFromTop(42);
    setupLabel_.setBounds(controllerRow.removeFromLeft(72));
    contextLabel_.setBounds(controllerRow.removeFromRight(200));
    const int controllerButtonWidth = controllerRow.getWidth() / static_cast<int>(controllerButtons_.size());
    for (auto& button : controllerButtons_)
        button.setBounds(controllerRow.removeFromLeft(controllerButtonWidth).reduced(3, 4));

    auto deviceRow = area.removeFromTop(40);
    deviceLabel_.setBounds(deviceRow.removeFromLeft(88));
    const int deviceSelectorWidth = deviceRow.getWidth() / static_cast<int>(deviceSelectors_.size());
    for (auto& selector : deviceSelectors_)
        selector.setBounds(deviceRow.removeFromLeft(deviceSelectorWidth).reduced(3, 6));

    area.removeFromTop(8);
    auto setupArea = area.removeFromTop(140);
    auto clipsArea = setupArea.removeFromLeft(170);
    setupArea.removeFromLeft(18);
    auto layersArea = setupArea.removeFromLeft(235);
    setupArea.removeFromLeft(18);
    auto channelArea = setupArea;

    clipsArea.removeFromTop(20);
    const int clipSize = 54;
    for (int clip = 0; clip < static_cast<int>(automationButtons_.size()); ++clip)
    {
        const int column = clip % 2;
        const int row = clip / 2;
        auto bounds = juce::Rectangle<int>(clipSize, clipSize)
            .withPosition(clipsArea.getX() + column * (clipSize + 14),
                          clipsArea.getY() + row * (clipSize + 12));
        if (automationButtons_[static_cast<size_t>(clip)] != nullptr)
            automationButtons_[static_cast<size_t>(clip)]->setBounds(bounds);
    }

    layersArea.removeFromTop(20);
    auto layerButtons = layersArea.removeFromTop(54);
    for (auto& button : layerButtons_)
        button.setBounds(layerButtons.removeFromLeft(52).reduced(4, 4));

    channelArea.removeFromTop(20);
    const int channelSize = 32;
    for (int channel = 0; channel < static_cast<int>(channelButtons_.size()); ++channel)
    {
        const int column = channel % 8;
        const int row = channel / 8;
        auto bounds = juce::Rectangle<int>(channelSize, channelSize)
            .withPosition(channelArea.getX() + column * (channelSize + 10),
                          channelArea.getY() + row * (channelSize + 12));
        if (channelButtons_[static_cast<size_t>(channel)] != nullptr)
            channelButtons_[static_cast<size_t>(channel)]->setBounds(bounds);
    }

    auto surfaceArea = area.removeFromTop(282);
    auto surfaceTop = surfaceArea.removeFromTop(28);
    surfaceLabel_.setBounds(surfaceTop.removeFromLeft(180));
    surfaceSummaryLabel_.setBounds(surfaceTop);
    surfaceArea.removeFromTop(18);
    surfaceViewport_.setBounds(surfaceArea);
    if (surfaceRowsContent_ != nullptr)
    {
        const int viewportWidth = std::max(1, surfaceViewport_.getMaximumVisibleWidth());
        surfaceRowsContent_->setSize(viewportWidth, SurfaceRowHeight * static_cast<int>(surfaceRows_.size()));
    }

    area.removeFromTop(12);
    if (activityView_ != nullptr)
        activityView_->setBounds(area.removeFromTop(76));
    else
        area.removeFromTop(76);
    area.removeFromTop(12);
    area.removeFromTop(22);

    for (auto& row : rows_)
    {
        auto rowBounds = area.removeFromTop(RowHeight);
        if (row != nullptr)
            row->setBounds(rowBounds);
        area.removeFromTop(8);
    }
}

void GalahadMidiToolsEditor::timerCallback()
{
    const bool captureState = hardwareCaptureButton_.getToggleState();
    if (captureState != lastHardwareCaptureState_)
    {
        lastHardwareCaptureState_ = captureState;
        processor_.refreshHardwareMidiInputs();
        refreshDeviceSelectors();
        updateHardwareStatus();
        lastHardwareInputCount_ = processor_.activeHardwareInputCount();
    }

    const int hardwareInputCount = processor_.activeHardwareInputCount();
    if (hardwareInputCount != lastHardwareInputCount_)
    {
        lastHardwareInputCount_ = hardwareInputCount;
        updateHardwareStatus();
    }

    if (const auto* layerValue = processor_.parameters().getRawParameterValue(galahad::plugin::ControllerLayerId))
    {
        const int activeLayer = juce::jlimit(0,
                                            galahad::plugin::ControllerLayerCount - 1,
                                            static_cast<int>(std::lround(layerValue->load(std::memory_order_relaxed))));
        if (activeLayer != selectedLayer_)
        {
            selectedLayer_ = activeLayer;
            refreshSurfaceRows();
            updateSetupState();
        }
    }

    if (const auto* patternValue = processor_.parameters().getRawParameterValue(galahad::plugin::ControllerPatternId))
    {
        const int activePattern = juce::jlimit(0,
                                              galahad::plugin::ControllerPatternCount - 1,
                                              static_cast<int>(std::lround(patternValue->load(std::memory_order_relaxed))));
        if (activePattern != selectedAutomationSlot_)
        {
            selectedAutomationSlot_ = activePattern;
            refreshSurfaceRows();
            updateSetupState();
        }
    }

    if (const auto* targetValue = processor_.parameters().getRawParameterValue(galahad::plugin::ControllerTargetChannelId))
    {
        const int activeTargetChannel = juce::jlimit(0,
                                                    galahad::plugin::ControllerTargetChannelCount - 1,
                                                    static_cast<int>(std::lround(targetValue->load(std::memory_order_relaxed)))) + 1;
        if (activeTargetChannel != selectedTargetChannel_)
        {
            selectedTargetChannel_ = activeTargetChannel;
            refreshSurfaceRows();
            updateSetupState();
        }
    }

    const int surfaceController = parameterIntValue(processor_.parameters(), galahad::plugin::SurfaceEditControllerId);
    const int surfaceControl = parameterIntValue(processor_.parameters(), galahad::plugin::SurfaceEditControlId);
    const int surfaceMap = parameterIntValue(processor_.parameters(), galahad::plugin::SurfaceEditLayerId);
    const int surfacePattern = parameterIntValue(processor_.parameters(), galahad::plugin::ControllerPatternId);
    const int surfaceTargetChannel = parameterIntValue(processor_.parameters(), galahad::plugin::ControllerTargetChannelId);
    if (surfaceController != lastSurfaceController_
        || surfaceControl != lastSurfaceControl_
        || surfaceMap != lastSurfaceMap_
        || surfacePattern != lastSurfacePattern_
        || surfaceTargetChannel != lastSurfaceTargetChannel_)
    {
        selectedControllerSlot_ = juce::jlimit(0, static_cast<int>(controllerButtons_.size()) - 1, surfaceController);
        lastSurfaceController_ = surfaceController;
        lastSurfaceControl_ = surfaceControl;
        lastSurfaceMap_ = surfaceMap;
        lastSurfacePattern_ = surfacePattern;
        lastSurfaceTargetChannel_ = surfaceTargetChannel;
        processor_.syncAndLoadSurfaceEditorSelection();
        refreshSurfaceRows();
        updateSetupState();
    }

    const auto input = processor_.lastControllerInput();
    if (input.serial != lastInputSerial_)
    {
        lastInputSerial_ = input.serial;
        activityView_->setInput(input);
    }

    const auto output = processor_.lastControllerOutput();
    if (output.serial != lastOutputSerial_)
    {
        lastOutputSerial_ = output.serial;
        activityView_->setOutput(output);
        if (output.slot >= 0 && output.slot < static_cast<int>(rows_.size()))
            rows_[static_cast<size_t>(output.slot)]->pulse(output.value);
    }

    if (learningSlot_ >= 0 && input.serial != learnStartSerial_ && input.controller >= 0)
        finishLearn(input);
    if (surfaceLearningControl_ >= 0 && input.serial != surfaceLearnStartSerial_ && input.controller >= 0)
        finishSurfaceLearn(input);

    activityView_->decay();
    for (auto& row : rows_)
        row->decay();
    updateSurfaceSummary();
}

void GalahadMidiToolsEditor::beginLearn(int slot)
{
    if (learningSlot_ == slot)
        learningSlot_ = -1;
    else
    {
        learningSlot_ = slot;
        learnStartSerial_ = processor_.lastControllerInput().serial;
    }

    refreshLearningState();
}

void GalahadMidiToolsEditor::beginSurfaceLearn(int control)
{
    if (surfaceLearningControl_ == control)
        surfaceLearningControl_ = -1;
    else
    {
        surfaceLearningControl_ = control;
        surfaceLearnStartSerial_ = processor_.lastControllerInput().serial;
        selectedSurfaceControl_ = juce::jlimit(0, galahad::plugin::ControllerSurfaceControlCount - 1, control);
    }

    refreshLearningState();
    refreshSurfaceRows();
}

void GalahadMidiToolsEditor::finishLearn(const GalahadMidiToolsProcessor::ControllerSnapshot& snapshot)
{
    if (learningSlot_ >= 0 && learningSlot_ < static_cast<int>(rows_.size()))
        rows_[static_cast<size_t>(learningSlot_)]->learnFrom(snapshot);

    learningSlot_ = -1;
    refreshLearningState();
}

void GalahadMidiToolsEditor::finishSurfaceLearn(const GalahadMidiToolsProcessor::ControllerSnapshot& snapshot)
{
    if (surfaceLearningControl_ >= 0 && surfaceLearningControl_ < static_cast<int>(surfaceRows_.size()))
        surfaceRows_[static_cast<size_t>(surfaceLearningControl_)]->learnFrom(snapshot);

    surfaceLearningControl_ = -1;
    refreshLearningState();
    refreshSurfaceRows();
    updateSurfaceSummary();
}

void GalahadMidiToolsEditor::refreshLearningState()
{
    for (int slot = 0; slot < static_cast<int>(rows_.size()); ++slot)
        rows_[static_cast<size_t>(slot)]->setLearning(slot == learningSlot_);

    for (int control = 0; control < static_cast<int>(surfaceRows_.size()); ++control)
        if (surfaceRows_[static_cast<size_t>(control)] != nullptr)
            surfaceRows_[static_cast<size_t>(control)]->setLearning(control == surfaceLearningControl_);
}

void GalahadMidiToolsEditor::refreshDeviceSelectors()
{
    refreshingDeviceSelectors_ = true;
    deviceSelectorIdentifiers_.clear();

    const auto devices = processor_.availableMidiInputs();
    for (const auto& device : devices)
        deviceSelectorIdentifiers_.add(device.identifier);

    const auto slotNames = processor_.controllerSlotDeviceNames();
    for (int slot = 0; slot < static_cast<int>(deviceSelectors_.size()); ++slot)
    {
        auto& selector = deviceSelectors_[static_cast<size_t>(slot)];
        selector.clear(juce::dontSendNotification);
        selector.addItem("None", 1);

        int selectedId = 1;
        const auto currentIdentifier = processor_.controllerSlotDeviceIdentifier(slot);
        for (int deviceIndex = 0; deviceIndex < static_cast<int>(devices.size()); ++deviceIndex)
        {
            const int itemId = deviceIndex + 2;
            selector.addItem(devices[static_cast<size_t>(deviceIndex)].name, itemId);
            if (devices[static_cast<size_t>(deviceIndex)].identifier == currentIdentifier)
                selectedId = itemId;
        }

        if (selectedId == 1 && currentIdentifier.isNotEmpty())
        {
            const auto missingName = slot < slotNames.size() ? slotNames[slot] : juce::String("Missing device");
            selectedId = 1000 + slot;
            selector.addItem(missingName, selectedId);
        }

        selector.setSelectedId(selectedId, juce::dontSendNotification);
    }

    refreshingDeviceSelectors_ = false;
}

void GalahadMidiToolsEditor::refreshSurfaceRows()
{
    for (auto& row : surfaceRows_)
        if (row != nullptr)
            row->refresh();
}

void GalahadMidiToolsEditor::updateHardwareStatus()
{
    hardwareLabel_.setText(hardwareStatusText(processor_.activeHardwareInputNames()), juce::dontSendNotification);

    updateSetupState();
}

void GalahadMidiToolsEditor::updateSetupState()
{
    for (int slot = 0; slot < static_cast<int>(controllerButtons_.size()); ++slot)
    {
        auto& button = controllerButtons_[static_cast<size_t>(slot)];
        button.setButtonText(controllerSlotText(slot));
        button.setToggleState(slot == selectedControllerSlot_, juce::dontSendNotification);
    }

    for (int layer = 0; layer < static_cast<int>(layerButtons_.size()); ++layer)
        layerButtons_[static_cast<size_t>(layer)].setToggleState(layer == selectedLayer_, juce::dontSendNotification);

    for (int channel = 0; channel < static_cast<int>(channelButtons_.size()); ++channel)
        if (channelButtons_[static_cast<size_t>(channel)] != nullptr)
            channelButtons_[static_cast<size_t>(channel)]->setToggleState(channel + 1 == selectedTargetChannel_, juce::dontSendNotification);

    for (int clip = 0; clip < static_cast<int>(automationButtons_.size()); ++clip)
        if (automationButtons_[static_cast<size_t>(clip)] != nullptr)
            automationButtons_[static_cast<size_t>(clip)]->setToggleState(clip == selectedAutomationSlot_, juce::dontSendNotification);

    const auto context = "C" + juce::String(selectedControllerSlot_ + 1)
        + "  L" + juce::String::charToString(static_cast<juce::juce_wchar>('A' + selectedLayer_))
        + "  Ch " + juce::String(selectedTargetChannel_)
        + "  Clip " + juce::String(selectedAutomationSlot_ + 1);
    contextLabel_.setText(context, juce::dontSendNotification);

    repaint();
}

void GalahadMidiToolsEditor::updateSurfaceSummary()
{
    const auto& parameters = processor_.parameters();
    const int controller = juce::jlimit(0,
                                       galahad::plugin::ControllerSlotCount - 1,
                                       parameterIntValue(parameters, galahad::plugin::SurfaceEditControllerId));
    const int control = juce::jlimit(0,
                                    galahad::plugin::ControllerSurfaceControlCount - 1,
                                    parameterIntValue(parameters, galahad::plugin::SurfaceEditControlId));
    const int layer = juce::jlimit(0,
                                  galahad::plugin::ControllerLayerCount - 1,
                                  parameterIntValue(parameters, galahad::plugin::SurfaceEditLayerId));
    const int pattern = juce::jlimit(0,
                                    galahad::plugin::ControllerPatternCount - 1,
                                    parameterIntValue(parameters, galahad::plugin::ControllerPatternId));
    const int targetChannel = juce::jlimit(0,
                                          galahad::plugin::ControllerTargetChannelCount - 1,
                                          parameterIntValue(parameters, galahad::plugin::ControllerTargetChannelId));
    const int enabled = parameterIntValue(parameters, galahad::plugin::SurfaceEditEnabledId);
    const int inputChannel = juce::jlimit(0, 16, parameterIntValue(parameters, galahad::plugin::SurfaceEditInputChannelId));
    const int inputCc = juce::jlimit(0, 127, parameterIntValue(parameters, galahad::plugin::SurfaceEditInputCcId));
    const int outputChannel = juce::jlimit(0, 15, parameterIntValue(parameters, galahad::plugin::SurfaceEditOutputChannelId));
    const int outputCc = juce::jlimit(0, 127, parameterIntValue(parameters, galahad::plugin::SurfaceEditOutputCcId));
    const int minimum = juce::jlimit(0, 127, parameterIntValue(parameters, galahad::plugin::SurfaceEditMinimumId));
    const int maximum = juce::jlimit(0, 127, parameterIntValue(parameters, galahad::plugin::SurfaceEditMaximumId));

    const auto inputText = inputChannel == 0 ? "Omni" : "Ch " + juce::String(inputChannel);
    const auto text = "C" + juce::String(controller + 1)
        + " " + galahad::plugin::controllerSurfaceControlName(control)
        + " P" + juce::String(pattern + 1)
        + " Ch " + juce::String(targetChannel + 1)
        + " Map " + juce::String(layer + 1)
        + ": " + (enabled >= 1 ? "On" : "Off")
        + "  " + inputText + " CC " + juce::String(inputCc)
        + " -> Ch " + juce::String(outputChannel + 1) + " CC " + juce::String(outputCc)
        + "  " + juce::String(minimum) + ".." + juce::String(maximum);
    surfaceSummaryLabel_.setText(text, juce::dontSendNotification);
}

juce::String GalahadMidiToolsEditor::controllerSlotText(int slot) const
{
    const auto names = processor_.controllerSlotDeviceNames();
    juce::String name = slot < names.size() ? names[slot] : "Slot " + juce::String(slot + 1);

    name = name.replace("Launch Control", "LaunchCtl")
               .replace("MIDI Mix", "MIDImix")
               .replace("MIDI MIX", "MIDImix");

    if (name.length() > 14)
        name = name.substring(0, 12) + "..";

    return juce::String(slot + 1) + " " + name;
}
