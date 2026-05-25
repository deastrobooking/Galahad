#include "GalahadMidiToolsProcessor.h"
#include "GalahadMidiToolsEditor.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace
{
constexpr const char* SeqRunId = "seqRun";
constexpr const char* SeqChannelId = "seqChannel";
constexpr const char* SeqChannelModeId = "seqChannelMode";
constexpr const char* SeqRootId = "seqRoot";
constexpr const char* SeqRateId = "seqRate";
constexpr const char* SeqStepsId = "seqSteps";
constexpr const char* SeqPulsesId = "seqPulses";
constexpr const char* SeqRotationId = "seqRotation";
constexpr const char* SeqProbabilityId = "seqProbability";
constexpr const char* SeqGateId = "seqGate";
constexpr const char* SeqVelocityId = "seqVelocity";

constexpr const char* LfoEnabledId = "lfoEnabled";
constexpr const char* LfoChannelId = "lfoChannel";
constexpr const char* LfoCcId = "lfoCc";
constexpr const char* LfoShapeId = "lfoShape";
constexpr const char* LfoRateId = "lfoRate";
constexpr const char* LfoMinimumId = "lfoMinimum";
constexpr const char* LfoMaximumId = "lfoMaximum";

constexpr const char* RouteEnabledId = "routeEnabled";
constexpr const char* RouteInputChannelId = "routeInputChannel";
constexpr const char* RouteOutputChannelId = "routeOutputChannel";
constexpr const char* RouteTransposeId = "routeTranspose";
constexpr const char* RouteVelocityId = "routeVelocity";
constexpr const char* RouteNotesId = "routeNotes";
constexpr const char* RouteCcsId = "routeCcs";

float valueOf(const juce::AudioProcessorValueTreeState& state, const char* id) noexcept
{
    if (const auto* value = state.getRawParameterValue(id))
        return value->load(std::memory_order_relaxed);

    return 0.0f;
}

uint8_t midiValueOf(const juce::AudioProcessorValueTreeState& state, const char* id) noexcept
{
    return static_cast<uint8_t>(juce::jlimit(0, 127, static_cast<int>(std::lround(valueOf(state, id)))));
}

uint8_t channelValueOf(const juce::AudioProcessorValueTreeState& state, const char* id) noexcept
{
    return static_cast<uint8_t>(juce::jlimit(1, 16, static_cast<int>(std::lround(valueOf(state, id)))));
}

bool boolValueOf(const juce::AudioProcessorValueTreeState& state, const char* id) noexcept
{
    return valueOf(state, id) >= 0.5f;
}

int parameterIntValue(const std::atomic<float>* parameter, int fallback) noexcept
{
    if (parameter == nullptr)
        return fallback;

    return static_cast<int>(std::lround(parameter->load(std::memory_order_relaxed)));
}

bool parameterBoolValue(const std::atomic<float>* parameter, bool fallback) noexcept
{
    if (parameter == nullptr)
        return fallback;

    return parameter->load(std::memory_order_relaxed) >= 0.5f;
}

uint8_t scaleControllerValue(uint8_t input, int minimum, int maximum) noexcept
{
    minimum = juce::jlimit(0, 127, minimum);
    maximum = juce::jlimit(0, 127, maximum);

    const int low = std::min(minimum, maximum);
    const int high = std::max(minimum, maximum);
    const int scaled = low + ((high - low) * static_cast<int>(input) + 63) / 127;
    return static_cast<uint8_t>(minimum <= maximum ? scaled : high - (scaled - low));
}

galahad::SequencerChannelMode sequencerChannelModeFromIndex(int index) noexcept
{
    switch (index)
    {
        case 1:
            return galahad::SequencerChannelMode::Rotate;
        case 2:
            return galahad::SequencerChannelMode::Random;
        case 3:
            return galahad::SequencerChannelMode::Step;
        case 0:
        default:
            return galahad::SequencerChannelMode::Fixed;
    }
}

galahad::LfoShape lfoShapeFromIndex(int index) noexcept
{
    switch (index)
    {
        case 1:
            return galahad::LfoShape::Triangle;
        case 2:
            return galahad::LfoShape::SawUp;
        case 3:
            return galahad::LfoShape::SawDown;
        case 4:
            return galahad::LfoShape::Square;
        case 5:
            return galahad::LfoShape::RandomSampleHold;
        case 0:
        default:
            return galahad::LfoShape::Sine;
    }
}
} // namespace

GalahadMidiToolsProcessor::GalahadMidiToolsProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_(*this, nullptr, "GalahadMidiTools", createParameterLayout())
{
    hardwareMidiCollector_.reset(sampleRate_);
    cacheControllerMapParameters();
    updateEngineConfig(120.0);
}

GalahadMidiToolsProcessor::~GalahadMidiToolsProcessor()
{
    closeHardwareMidiInputs();
}

void GalahadMidiToolsProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    sampleRate_ = sampleRate;
    engine_.reset();
    launchedCells_.reset();
    hardwareMidiCollector_.reset(sampleRate_);
    ensureVirtualMidiOutput();
    refreshHardwareMidiInputs();
}

void GalahadMidiToolsProcessor::releaseResources()
{
    launchedCells_.reset();
}

bool GalahadMidiToolsProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    if (!input.isDisabled())
        return false;

    return output == juce::AudioChannelSet::mono()
        || output == juce::AudioChannelSet::stereo();
}

void GalahadMidiToolsProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();
    ensureVirtualMidiOutput();

    std::array<galahad::MidiEvent, MaxBlockEvents> incoming{};
    std::array<galahad::MidiEvent, MaxBlockEvents> mapped{};
    std::array<galahad::MidiEvent, MaxBlockEvents> processed{};

    double bpm = 120.0;
    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto positionBpm = position->getBpm(); positionBpm && *positionBpm > 0.0)
                bpm = *positionBpm;
        }
    }
    updateEngineConfig(bpm);

    size_t incomingCount = 0;
    size_t mappedCount = 0;
    auto ingestMessage = [this, &incoming, &incomingCount, &mapped, &mappedCount](const juce::MidiMessage& message, int samplePosition) {
        if (message.isNoteOnOrOff() || message.isController())
        {
            const auto event = fromJuceMidi(message, samplePosition);
            bool mappedSource = false;

            if (event.type == galahad::MidiEventType::ControlChange)
            {
                recordControllerInput(event);
                mappedSource = appendControllerMappings(event,
                                                        std::span<galahad::MidiEvent>(mapped.data(), mapped.size()),
                                                        mappedCount);
            }

            if ((!mappedSource || shouldPassMappedSource()) && incomingCount < incoming.size())
                incoming[incomingCount++] = event;
        }

        if (auto cell = galahad::midi::MidiProtocol::sessionCellForMessage(message))
        {
            launchedCells_.tryPush(*cell);
            lastTrack_.store(cell->track, std::memory_order_relaxed);
            lastScene_.store(cell->scene, std::memory_order_relaxed);
            launchSerial_.fetch_add(1, std::memory_order_relaxed);
        }
    };

    for (const auto metadata : midiMessages)
        ingestMessage(metadata.getMessage(), metadata.samplePosition);

    juce::MidiBuffer hardwareMessages;
    hardwareMidiCollector_.removeNextBlockOfMessages(hardwareMessages, buffer.getNumSamples());
    if (shouldCaptureHardwareInputs())
    {
        for (const auto metadata : hardwareMessages)
            ingestMessage(metadata.getMessage(), metadata.samplePosition);
    }

    size_t processedCount = engine_.process(std::span<const galahad::MidiEvent>(incoming.data(), incomingCount),
                                            static_cast<int>(sampleRate_),
                                            buffer.getNumSamples(),
                                            std::span<galahad::MidiEvent>(processed.data(), processed.size()));

    galahad::midi::SessionCell cell;
    while (processedCount < processed.size() && launchedCells_.tryPop(cell))
        processed[processedCount++] = fromJuceMidi(galahad::midi::MidiProtocol::clipFeedbackNote(cell.track, cell.scene, 127), 0);

    midiMessages.clear();
    juce::MidiBuffer virtualOutput;
    for (size_t i = 0; i < processedCount; ++i)
    {
        const auto message = toJuceMidi(processed[i]);
        midiMessages.addEvent(message, processed[i].sampleOffset);
        virtualOutput.addEvent(message, processed[i].sampleOffset);
    }
    for (size_t i = 0; i < mappedCount; ++i)
    {
        const auto message = toJuceMidi(mapped[i]);
        midiMessages.addEvent(message, mapped[i].sampleOffset);
        virtualOutput.addEvent(message, mapped[i].sampleOffset);
    }

    if (virtualMidiOutput_ != nullptr && virtualOutput.getNumEvents() > 0)
        virtualMidiOutput_->sendBlockOfMessagesNow(virtualOutput);
}

juce::AudioProcessorEditor* GalahadMidiToolsProcessor::createEditor()
{
    return new GalahadMidiToolsEditor(*this);
}

GalahadMidiToolsProcessor::ControllerSnapshot GalahadMidiToolsProcessor::lastControllerInput() const noexcept
{
    const int serial = lastControllerInputSerial_.load(std::memory_order_acquire);
    return ControllerSnapshot{ lastControllerInputChannel_.load(std::memory_order_relaxed),
                               lastControllerInputController_.load(std::memory_order_relaxed),
                               lastControllerInputValue_.load(std::memory_order_relaxed),
                               -1,
                               serial };
}

GalahadMidiToolsProcessor::ControllerSnapshot GalahadMidiToolsProcessor::lastControllerOutput() const noexcept
{
    const int serial = lastControllerOutputSerial_.load(std::memory_order_acquire);
    return ControllerSnapshot{ lastControllerOutputChannel_.load(std::memory_order_relaxed),
                               lastControllerOutputController_.load(std::memory_order_relaxed),
                               lastControllerOutputValue_.load(std::memory_order_relaxed),
                               lastControllerOutputSlot_.load(std::memory_order_relaxed),
                               serial };
}

void GalahadMidiToolsProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = parameters_.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void GalahadMidiToolsProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(parameters_.state.getType()))
            parameters_.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout GalahadMidiToolsProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto addBool = [&params](const char* id, const juce::String& name, bool defaultValue) {
        params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(id, 1), name, defaultValue));
    };

    auto addInt = [&params](const char* id, const juce::String& name, int minimum, int maximum, int defaultValue) {
        params.push_back(std::make_unique<juce::AudioParameterInt>(juce::ParameterID(id, 1),
                                                                   name,
                                                                   minimum,
                                                                   maximum,
                                                                   defaultValue));
    };

    auto addFloat = [&params](const char* id,
                              const juce::String& name,
                              float minimum,
                              float maximum,
                              float defaultValue,
                              float interval = 0.01f) {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(id, 1),
                                                                     name,
                                                                     juce::NormalisableRange<float>(minimum, maximum, interval),
                                                                     defaultValue));
    };

    addBool(SeqRunId, "Seq Run", true);
    addInt(SeqChannelId, "Seq Ch", 1, 16, 1);
    params.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(SeqChannelModeId, 1),
                                                                  "Seq Ch Mode",
                                                                  juce::StringArray{ "Fixed", "Rotate", "Random", "Step" },
                                                                  0));
    addInt(SeqRootId, "Seq Root", 0, 127, 48);
    addFloat(SeqRateId, "Seq Rate", 0.25f, 16.0f, 4.0f, 0.25f);
    addInt(SeqStepsId, "Seq Steps", 1, static_cast<int>(galahad::AlgorithmicSequencer::MaxSteps), 16);
    addInt(SeqPulsesId, "Seq Pulses", 0, static_cast<int>(galahad::AlgorithmicSequencer::MaxSteps), 5);
    addInt(SeqRotationId, "Seq Rotate", 0, static_cast<int>(galahad::AlgorithmicSequencer::MaxSteps - 1), 0);
    addInt(SeqProbabilityId, "Seq Prob", 0, 127, 127);
    addInt(SeqGateId, "Seq Gate", 1, 100, 45);
    addInt(SeqVelocityId, "Seq Vel", 1, 127, 96);

    addBool(LfoEnabledId, "LFO On", true);
    addInt(LfoChannelId, "LFO Ch", 1, 16, 1);
    addInt(LfoCcId, "LFO CC", 0, 127, 74);
    params.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(LfoShapeId, 1),
                                                                  "LFO Shape",
                                                                  juce::StringArray{ "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample Hold" },
                                                                  1));
    addFloat(LfoRateId, "LFO Rate", 0.01f, 20.0f, 0.2f, 0.01f);
    addInt(LfoMinimumId, "LFO Min", 0, 127, 24);
    addInt(LfoMaximumId, "LFO Max", 0, 127, 104);

    addBool(RouteEnabledId, "Route On", true);
    addInt(RouteInputChannelId, "Route In Ch", 0, 16, 0);
    addInt(RouteOutputChannelId, "Route Out Ch", 1, 16, 1);
    addInt(RouteTransposeId, "Route Transpose", -48, 48, 0);
    addInt(RouteVelocityId, "Route Vel", -127, 127, 0);
    addBool(RouteNotesId, "Route Notes", true);
    addBool(RouteCcsId, "Route CCs", true);

    addBool(galahad::plugin::HardwareCaptureId, "Hardware Capture", true);
    addBool(galahad::plugin::MapThruId, "Map Thru", false);
    const auto inputChannelChoices = galahad::plugin::inputChannelChoices();
    const auto outputChannelChoices = galahad::plugin::outputChannelChoices();
    for (int slot = 0; slot < galahad::plugin::ControllerMapSlotCount; ++slot)
    {
        const juce::String slotName = "Map " + juce::String(slot + 1);
        addBool(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapEnabledSuffix).toRawUTF8(),
                slotName + " On",
                false);
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapInputChannelSuffix), 1),
            slotName + " In Ch",
            inputChannelChoices,
            0));
        addInt(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapInputCcSuffix).toRawUTF8(),
               slotName + " In CC",
               0,
               127,
               slot);
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapOutputChannelSuffix), 1),
            slotName + " Out Ch",
            outputChannelChoices,
            slot % 16));
        addInt(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapOutputCcSuffix).toRawUTF8(),
               slotName + " Out CC",
               0,
               127,
               20 + slot);
        addInt(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapMinimumSuffix).toRawUTF8(),
               slotName + " Min",
               0,
               127,
               0);
        addInt(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapMaximumSuffix).toRawUTF8(),
               slotName + " Max",
               0,
               127,
               127);
    }

    return { params.begin(), params.end() };
}

void GalahadMidiToolsProcessor::updateEngineConfig(double bpm)
{
    galahad::MidiToolsEngineConfig config;
    config.bpm = bpm;

    config.sequencer.enabled = boolValueOf(parameters_, SeqRunId);
    config.sequencer.channel = channelValueOf(parameters_, SeqChannelId);
    config.sequencer.channelMode = sequencerChannelModeFromIndex(static_cast<int>(std::lround(valueOf(parameters_, SeqChannelModeId))));
    config.sequencer.rootNote = midiValueOf(parameters_, SeqRootId);
    config.sequencer.rateDivisor = static_cast<double>(valueOf(parameters_, SeqRateId));
    config.sequencer.steps = static_cast<uint8_t>(juce::jlimit(1, static_cast<int>(galahad::AlgorithmicSequencer::MaxSteps), static_cast<int>(std::lround(valueOf(parameters_, SeqStepsId)))));
    config.sequencer.pulses = static_cast<uint8_t>(juce::jlimit(0, static_cast<int>(config.sequencer.steps), static_cast<int>(std::lround(valueOf(parameters_, SeqPulsesId)))));
    config.sequencer.rotation = static_cast<uint8_t>(juce::jlimit(0, static_cast<int>(config.sequencer.steps - 1), static_cast<int>(std::lround(valueOf(parameters_, SeqRotationId)))));
    config.sequencer.probability = midiValueOf(parameters_, SeqProbabilityId);
    config.sequencer.gatePercent = static_cast<uint8_t>(juce::jlimit(1, 100, static_cast<int>(std::lround(valueOf(parameters_, SeqGateId)))));
    config.sequencer.velocity = midiValueOf(parameters_, SeqVelocityId);

    config.lfo.enabled = boolValueOf(parameters_, LfoEnabledId);
    config.lfo.channel = channelValueOf(parameters_, LfoChannelId);
    config.lfo.controller = midiValueOf(parameters_, LfoCcId);
    config.lfo.shape = lfoShapeFromIndex(static_cast<int>(std::lround(valueOf(parameters_, LfoShapeId))));
    config.lfo.rateHz = static_cast<double>(valueOf(parameters_, LfoRateId));
    config.lfo.minimum = midiValueOf(parameters_, LfoMinimumId);
    config.lfo.maximum = midiValueOf(parameters_, LfoMaximumId);

    config.router.enabled = boolValueOf(parameters_, RouteEnabledId);
    config.router.inputChannel = static_cast<uint8_t>(juce::jlimit(0, 16, static_cast<int>(std::lround(valueOf(parameters_, RouteInputChannelId)))));
    config.router.outputChannel = channelValueOf(parameters_, RouteOutputChannelId);
    config.router.transpose = juce::jlimit(-48, 48, static_cast<int>(std::lround(valueOf(parameters_, RouteTransposeId))));
    config.router.velocityOffset = juce::jlimit(-127, 127, static_cast<int>(std::lround(valueOf(parameters_, RouteVelocityId))));
    config.router.passNotes = boolValueOf(parameters_, RouteNotesId);
    config.router.passControlChanges = boolValueOf(parameters_, RouteCcsId);

    engine_.setConfig(config);
}

void GalahadMidiToolsProcessor::ensureVirtualMidiOutput()
{
    if (virtualMidiOutput_ != nullptr)
        return;

   #if JUCE_MAC || JUCE_LINUX || JUCE_BSD || JUCE_IOS
    virtualMidiOutput_ = juce::MidiOutput::createNewDevice("Galahad 1");
   #endif
}

void GalahadMidiToolsProcessor::refreshHardwareMidiInputs()
{
    const std::lock_guard lock(hardwareMidiInputsMutex_);

    for (auto& input : hardwareMidiInputs_)
    {
        if (input != nullptr)
            input->stop();
    }
    hardwareMidiInputs_.clear();
    activeHardwareInputNames_.clear();

    if (!shouldCaptureHardwareInputs())
    {
        activeHardwareInputCount_.store(0, std::memory_order_relaxed);
        return;
    }

    for (const auto& device : juce::MidiInput::getAvailableDevices())
    {
        if (!isPreferredHardwareInput(device))
            continue;

        auto input = juce::MidiInput::openDevice(device.identifier, &hardwareMidiCollector_);
        if (input == nullptr)
            continue;

        input->start();
        activeHardwareInputNames_.add(device.name);
        hardwareMidiInputs_.push_back(std::move(input));
    }

    activeHardwareInputCount_.store(static_cast<int>(hardwareMidiInputs_.size()), std::memory_order_relaxed);
}

juce::StringArray GalahadMidiToolsProcessor::activeHardwareInputNames() const
{
    const std::lock_guard lock(hardwareMidiInputsMutex_);
    return activeHardwareInputNames_;
}

void GalahadMidiToolsProcessor::closeHardwareMidiInputs()
{
    const std::lock_guard lock(hardwareMidiInputsMutex_);

    for (auto& input : hardwareMidiInputs_)
    {
        if (input != nullptr)
            input->stop();
    }

    hardwareMidiInputs_.clear();
    activeHardwareInputNames_.clear();
    activeHardwareInputCount_.store(0, std::memory_order_relaxed);
}

bool GalahadMidiToolsProcessor::shouldCaptureHardwareInputs() const noexcept
{
    return boolValueOf(parameters_, galahad::plugin::HardwareCaptureId);
}

bool GalahadMidiToolsProcessor::isPreferredHardwareInput(const juce::MidiDeviceInfo& device)
{
    const auto name = device.name.toLowerCase();

    if (name.contains("galahad"))
        return false;

    return name.contains("midimix")
        || name.contains("midi mix")
        || name.contains("launch control")
        || name.contains("akai")
        || name.contains("novation");
}

void GalahadMidiToolsProcessor::cacheControllerMapParameters()
{
    for (int slot = 0; slot < ControllerMapSlotCount; ++slot)
    {
        auto& map = controllerMapParameters_[static_cast<size_t>(slot)];
        map.enabled = parameters_.getRawParameterValue(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapEnabledSuffix));
        map.inputChannel = parameters_.getRawParameterValue(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapInputChannelSuffix));
        map.inputCc = parameters_.getRawParameterValue(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapInputCcSuffix));
        map.outputChannel = parameters_.getRawParameterValue(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapOutputChannelSuffix));
        map.outputCc = parameters_.getRawParameterValue(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapOutputCcSuffix));
        map.minimum = parameters_.getRawParameterValue(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapMinimumSuffix));
        map.maximum = parameters_.getRawParameterValue(galahad::plugin::controllerMapParameterId(slot, galahad::plugin::MapMaximumSuffix));
    }
}

bool GalahadMidiToolsProcessor::appendControllerMappings(const galahad::MidiEvent& event,
                                                         std::span<galahad::MidiEvent> output,
                                                         size_t& outputCount) noexcept
{
    bool matched = false;

    for (int slot = 0; slot < ControllerMapSlotCount && outputCount < output.size(); ++slot)
    {
        const auto& map = controllerMapParameters_[static_cast<size_t>(slot)];
        if (!parameterBoolValue(map.enabled, false))
            continue;

        const int inputChannel = juce::jlimit(0, 16, parameterIntValue(map.inputChannel, 0));
        const int inputCc = juce::jlimit(0, 127, parameterIntValue(map.inputCc, 0));

        if (inputCc != event.data1)
            continue;

        if (inputChannel != 0 && inputChannel != event.channel)
            continue;

        const int outputChannelIndex = juce::jlimit(0, 15, parameterIntValue(map.outputChannel, 0));
        const uint8_t outputChannel = static_cast<uint8_t>(outputChannelIndex + 1);
        const uint8_t outputCc = static_cast<uint8_t>(juce::jlimit(0, 127, parameterIntValue(map.outputCc, event.data1)));
        const auto mapped = galahad::MidiEvent{ galahad::MidiEventType::ControlChange,
                                                outputChannel,
                                                outputCc,
                                                scaleControllerValue(event.data2,
                                                                     parameterIntValue(map.minimum, 0),
                                                                     parameterIntValue(map.maximum, 127)),
                                                event.sampleOffset };

        output[outputCount++] = mapped;
        recordControllerOutput(mapped, slot);
        matched = true;
    }

    return matched;
}

bool GalahadMidiToolsProcessor::shouldPassMappedSource() const noexcept
{
    return boolValueOf(parameters_, galahad::plugin::MapThruId);
}

void GalahadMidiToolsProcessor::recordControllerInput(const galahad::MidiEvent& event) noexcept
{
    lastControllerInputChannel_.store(event.channel, std::memory_order_relaxed);
    lastControllerInputController_.store(event.data1, std::memory_order_relaxed);
    lastControllerInputValue_.store(event.data2, std::memory_order_relaxed);
    lastControllerInputSerial_.fetch_add(1, std::memory_order_release);
}

void GalahadMidiToolsProcessor::recordControllerOutput(const galahad::MidiEvent& event, int slot) noexcept
{
    lastControllerOutputChannel_.store(event.channel, std::memory_order_relaxed);
    lastControllerOutputController_.store(event.data1, std::memory_order_relaxed);
    lastControllerOutputValue_.store(event.data2, std::memory_order_relaxed);
    lastControllerOutputSlot_.store(slot, std::memory_order_relaxed);
    lastControllerOutputSerial_.fetch_add(1, std::memory_order_release);
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
