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

const juce::Identifier SurfaceMapsStateId{ "ControllerSurfaceMaps" };
const juce::Identifier SurfaceMapStateId{ "Map" };
const juce::Identifier ControllerDeviceSlotsStateId{ "ControllerDeviceSlots" };
const juce::Identifier ControllerDeviceSlotStateId{ "Slot" };

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
    initializeControllerSurfaceMaps();
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
    syncSurfaceEditorToSelectedMap();

    std::array<galahad::MidiEvent, MaxBlockEvents> incoming{};
    std::array<galahad::MidiEvent, MaxBlockEvents> mapped{};
    std::array<galahad::MidiEvent, MaxBlockEvents> processed{};
    size_t incomingCount = 0;
    size_t mappedCount = 0;

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

    const auto surfaceContext = currentSurfacePerformanceContext();
    const bool shouldRecallSurface = ! hasSurfacePerformanceContext_
        || (hasSurfacePerformanceContext_
        && (surfaceContext.layer != lastSurfacePerformanceContext_.layer
            || surfaceContext.pattern != lastSurfacePerformanceContext_.pattern
            || surfaceContext.targetChannel != lastSurfacePerformanceContext_.targetChannel));
    if (shouldRecallSurface)
        appendSurfaceRecallEvents(surfaceContext, std::span<galahad::MidiEvent>(mapped.data(), mapped.size()), mappedCount);
    lastSurfacePerformanceContext_ = surfaceContext;
    hasSurfacePerformanceContext_ = true;

    auto ingestMessage = [this, &incoming, &incomingCount, &mapped, &mappedCount](const juce::MidiMessage& message,
                                                                                  int samplePosition,
                                                                                  int sourceControllerSlot) {
        if (message.isNoteOnOrOff() || message.isController())
        {
            const auto event = fromJuceMidi(message, samplePosition);
            bool mappedSource = false;

            if (event.type == galahad::MidiEventType::ControlChange)
            {
                recordControllerInput(event);
                mappedSource = appendControllerMappings(event,
                                                        sourceControllerSlot,
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
        ingestMessage(metadata.getMessage(), metadata.samplePosition, -1);

    if (shouldCaptureHardwareInputs())
    {
        const std::lock_guard lock(hardwareMidiInputsMutex_);
        for (auto& hardware : hardwareMidiInputs_)
        {
            if (hardware.collector == nullptr)
                continue;

            juce::MidiBuffer hardwareMessages;
            hardware.collector->removeNextBlockOfMessages(hardwareMessages, buffer.getNumSamples());
            for (const auto metadata : hardwareMessages)
                ingestMessage(metadata.getMessage(), metadata.samplePosition, hardware.slot);
        }
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
    syncSurfaceEditorToSelectedMap();
    if (auto state = parameters_.copyState(); state.isValid())
    {
        state.addChild(createControllerSurfaceMapsState(), -1, nullptr);
        state.addChild(createControllerDeviceSlotsState(), -1, nullptr);
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void GalahadMidiToolsProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(parameters_.state.getType()))
        {
            auto state = juce::ValueTree::fromXml(*xml);
            const auto surfaceMapsState = state.getChildWithName(SurfaceMapsStateId);
            const auto controllerDeviceSlotsState = state.getChildWithName(ControllerDeviceSlotsStateId);
            restoreControllerSurfaceMapsState(surfaceMapsState);
            if (surfaceMapsState.isValid())
                state.removeChild(surfaceMapsState, nullptr);
            if (controllerDeviceSlotsState.isValid())
                state.removeChild(controllerDeviceSlotsState, nullptr);
            parameters_.replaceState(state);
            restoreControllerDeviceSlotsState(controllerDeviceSlotsState);
            refreshHardwareMidiInputs();
        }
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

    const auto inputChannelChoices = galahad::plugin::inputChannelChoices();
    const auto outputChannelChoices = galahad::plugin::outputChannelChoices();

    addBool(galahad::plugin::HardwareCaptureId, "Hardware Capture", true);
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(galahad::plugin::ControllerLayerId, 1),
        "Controller Layer",
        galahad::plugin::controllerLayerChoices(),
        0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(galahad::plugin::ControllerPatternId, 1),
        "Controller Pattern",
        galahad::plugin::controllerPatternChoices(),
        0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(galahad::plugin::ControllerTargetChannelId, 1),
        "Controller Target Channel",
        galahad::plugin::controllerTargetChannelChoices(),
        0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(galahad::plugin::SurfaceEditControllerId, 1),
        "Surface Edit Controller",
        galahad::plugin::controllerSlotChoices(),
        0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(galahad::plugin::SurfaceEditControlId, 1),
        "Surface Edit Control",
        galahad::plugin::controllerSurfaceControlChoices(),
        0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(galahad::plugin::SurfaceEditLayerId, 1),
        "Surface Edit Map",
        galahad::plugin::controllerLayerChoices(),
        0));
    addBool(galahad::plugin::SurfaceEditEnabledId, "Surface Map On", false);
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(galahad::plugin::SurfaceEditInputChannelId, 1),
        "Surface In Ch",
        inputChannelChoices,
        0));
    addInt(galahad::plugin::SurfaceEditInputCcId, "Surface In CC", 0, 127, 0);
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(galahad::plugin::SurfaceEditOutputChannelId, 1),
        "Surface Out Ch",
        outputChannelChoices,
        0));
    addInt(galahad::plugin::SurfaceEditOutputCcId, "Surface Out CC", 0, 127, 0);
    addInt(galahad::plugin::SurfaceEditMinimumId, "Surface Min", 0, 127, 0);
    addInt(galahad::plugin::SurfaceEditMaximumId, "Surface Max", 0, 127, 127);
    addBool(galahad::plugin::MapThruId, "Map Thru", false);
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
    const auto devices = availableMidiInputs();
    const std::lock_guard lock(hardwareMidiInputsMutex_);

    for (auto& hardware : hardwareMidiInputs_)
    {
        if (hardware.input != nullptr)
            hardware.input->stop();
    }
    hardwareMidiInputs_.clear();
    activeHardwareInputNames_.clear();
    assignDefaultControllerSlotsIfNeeded(devices);

    if (!shouldCaptureHardwareInputs())
    {
        activeHardwareInputCount_.store(0, std::memory_order_relaxed);
        return;
    }

    juce::StringArray openedIdentifiers;
    for (int slot = 0; slot < ControllerSlotCount; ++slot)
    {
        const auto identifier = controllerSlotDeviceIdentifiers_[static_cast<size_t>(slot)];
        if (identifier.isEmpty() || openedIdentifiers.contains(identifier))
            continue;

        const juce::MidiDeviceInfo* selectedDevice = nullptr;
        for (const auto& device : devices)
        {
            if (device.identifier == identifier)
            {
                selectedDevice = &device;
                break;
            }
        }

        if (selectedDevice == nullptr)
            continue;

        HardwareMidiInput hardware;
        hardware.slot = slot;
        hardware.name = selectedDevice->name;
        hardware.collector = std::make_unique<juce::MidiMessageCollector>();
        hardware.collector->reset(sampleRate_);
        hardware.input = juce::MidiInput::openDevice(selectedDevice->identifier, hardware.collector.get());
        if (hardware.input == nullptr)
            continue;

        hardware.input->start();
        activeHardwareInputNames_.add("C" + juce::String(slot + 1) + ": " + selectedDevice->name);
        hardwareMidiInputs_.push_back(std::move(hardware));
        openedIdentifiers.add(identifier);
    }

    activeHardwareInputCount_.store(static_cast<int>(hardwareMidiInputs_.size()), std::memory_order_relaxed);
}

std::vector<juce::MidiDeviceInfo> GalahadMidiToolsProcessor::availableMidiInputs() const
{
    std::vector<juce::MidiDeviceInfo> devices;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        devices.push_back(device);

    return devices;
}

juce::StringArray GalahadMidiToolsProcessor::activeHardwareInputNames() const
{
    const std::lock_guard lock(hardwareMidiInputsMutex_);
    return activeHardwareInputNames_;
}

juce::StringArray GalahadMidiToolsProcessor::controllerSlotDeviceNames() const
{
    const std::lock_guard lock(hardwareMidiInputsMutex_);

    juce::StringArray names;
    for (const auto& name : controllerSlotDeviceNames_)
        names.add(name.isNotEmpty() ? name : "None");

    return names;
}

juce::String GalahadMidiToolsProcessor::controllerSlotDeviceIdentifier(int slot) const
{
    slot = juce::jlimit(0, ControllerSlotCount - 1, slot);
    const std::lock_guard lock(hardwareMidiInputsMutex_);
    return controllerSlotDeviceIdentifiers_[static_cast<size_t>(slot)];
}

void GalahadMidiToolsProcessor::setControllerSlotDeviceIdentifier(int slot, const juce::String& identifier)
{
    slot = juce::jlimit(0, ControllerSlotCount - 1, slot);
    const auto devices = availableMidiInputs();

    juce::String resolvedName;
    for (const auto& device : devices)
    {
        if (device.identifier == identifier)
        {
            resolvedName = device.name;
            break;
        }
    }

    {
        const std::lock_guard lock(hardwareMidiInputsMutex_);
        auto& slotIdentifier = controllerSlotDeviceIdentifiers_[static_cast<size_t>(slot)];
        auto& slotName = controllerSlotDeviceNames_[static_cast<size_t>(slot)];

        controllerSlotAssignmentsManual_ = true;
        slotIdentifier = identifier;
        if (identifier.isEmpty())
            slotName.clear();
        else if (resolvedName.isNotEmpty())
            slotName = resolvedName;
        else if (slotName.isEmpty())
            slotName = "Missing device";
    }

    refreshHardwareMidiInputs();
}

juce::ValueTree GalahadMidiToolsProcessor::createControllerDeviceSlotsState() const
{
    juce::ValueTree state{ ControllerDeviceSlotsStateId };

    const std::lock_guard lock(hardwareMidiInputsMutex_);
    state.setProperty("manual", controllerSlotAssignmentsManual_ ? 1 : 0, nullptr);

    if (!controllerSlotAssignmentsManual_)
        return state;

    for (int slot = 0; slot < ControllerSlotCount; ++slot)
    {
        const auto& identifier = controllerSlotDeviceIdentifiers_[static_cast<size_t>(slot)];
        const auto& name = controllerSlotDeviceNames_[static_cast<size_t>(slot)];
        if (identifier.isEmpty() && name.isEmpty())
            continue;

        juce::ValueTree child{ ControllerDeviceSlotStateId };
        child.setProperty("slot", slot, nullptr);
        child.setProperty("identifier", identifier, nullptr);
        child.setProperty("name", name, nullptr);
        state.addChild(child, -1, nullptr);
    }

    return state;
}

void GalahadMidiToolsProcessor::restoreControllerDeviceSlotsState(const juce::ValueTree& state)
{
    const std::lock_guard lock(hardwareMidiInputsMutex_);

    for (auto& identifier : controllerSlotDeviceIdentifiers_)
        identifier.clear();
    for (auto& name : controllerSlotDeviceNames_)
        name.clear();
    controllerSlotAssignmentsManual_ = false;

    if (!state.isValid())
        return;

    controllerSlotAssignmentsManual_ = static_cast<int>(state.getProperty("manual", 0)) != 0;
    for (int childIndex = 0; childIndex < state.getNumChildren(); ++childIndex)
    {
        const auto child = state.getChild(childIndex);
        if (!child.hasType(ControllerDeviceSlotStateId))
            continue;

        const int slot = juce::jlimit(0, ControllerSlotCount - 1, static_cast<int>(child.getProperty("slot", 0)));
        controllerSlotDeviceIdentifiers_[static_cast<size_t>(slot)] = child.getProperty("identifier", juce::var{}).toString();
        controllerSlotDeviceNames_[static_cast<size_t>(slot)] = child.getProperty("name", juce::var{}).toString();
    }
}

void GalahadMidiToolsProcessor::assignDefaultControllerSlotsIfNeeded(const std::vector<juce::MidiDeviceInfo>& devices)
{
    if (!controllerSlotAssignmentsManual_)
    {
        for (auto& identifier : controllerSlotDeviceIdentifiers_)
            identifier.clear();
        for (auto& name : controllerSlotDeviceNames_)
            name.clear();

        int slot = 0;
        for (const auto& device : devices)
        {
            if (slot >= ControllerSlotCount)
                break;

            if (!isPreferredHardwareInput(device))
                continue;

            controllerSlotDeviceIdentifiers_[static_cast<size_t>(slot)] = device.identifier;
            controllerSlotDeviceNames_[static_cast<size_t>(slot)] = device.name;
            ++slot;
        }

        return;
    }

    for (int slot = 0; slot < ControllerSlotCount; ++slot)
    {
        const auto& identifier = controllerSlotDeviceIdentifiers_[static_cast<size_t>(slot)];
        auto& name = controllerSlotDeviceNames_[static_cast<size_t>(slot)];

        if (identifier.isEmpty())
        {
            name.clear();
            continue;
        }

        juce::String resolvedName;
        for (const auto& device : devices)
        {
            if (device.identifier == identifier)
            {
                resolvedName = device.name;
                break;
            }
        }

        if (resolvedName.isNotEmpty())
        {
            name = resolvedName;
        }
        else if (name.isEmpty())
        {
            name = "Missing device";
        }
        else if (!name.endsWithIgnoreCase(" (missing)"))
        {
            name += " (missing)";
        }
    }
}

void GalahadMidiToolsProcessor::closeHardwareMidiInputs()
{
    const std::lock_guard lock(hardwareMidiInputsMutex_);

    for (auto& hardware : hardwareMidiInputs_)
    {
        if (hardware.input != nullptr)
            hardware.input->stop();
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

    surfaceEditorParameters_.controller = parameters_.getRawParameterValue(galahad::plugin::SurfaceEditControllerId);
    surfaceEditorParameters_.control = parameters_.getRawParameterValue(galahad::plugin::SurfaceEditControlId);
    surfaceEditorParameters_.layer = parameters_.getRawParameterValue(galahad::plugin::SurfaceEditLayerId);
    surfaceEditorParameters_.enabled = parameters_.getRawParameterValue(galahad::plugin::SurfaceEditEnabledId);
    surfaceEditorParameters_.inputChannel = parameters_.getRawParameterValue(galahad::plugin::SurfaceEditInputChannelId);
    surfaceEditorParameters_.inputCc = parameters_.getRawParameterValue(galahad::plugin::SurfaceEditInputCcId);
    surfaceEditorParameters_.outputChannel = parameters_.getRawParameterValue(galahad::plugin::SurfaceEditOutputChannelId);
    surfaceEditorParameters_.outputCc = parameters_.getRawParameterValue(galahad::plugin::SurfaceEditOutputCcId);
    surfaceEditorParameters_.minimum = parameters_.getRawParameterValue(galahad::plugin::SurfaceEditMinimumId);
    surfaceEditorParameters_.maximum = parameters_.getRawParameterValue(galahad::plugin::SurfaceEditMaximumId);
}

void GalahadMidiToolsProcessor::initializeControllerSurfaceMaps() noexcept
{
    for (int controller = 0; controller < ControllerSlotCount; ++controller)
    {
        for (int pattern = 0; pattern < ControllerPatternCount; ++pattern)
        {
            for (int targetChannel = 0; targetChannel < ControllerTargetChannelCount; ++targetChannel)
            {
                for (int control = 0; control < ControllerSurfaceControlCount; ++control)
                {
                    for (int layer = 0; layer < ControllerLayerCount; ++layer)
                    {
                        const int index = galahad::plugin::controllerSurfaceMapIndex(controller, pattern, targetChannel, control, layer);
                        auto& map = controllerSurfaceMaps_[static_cast<size_t>(index)];
                        const int defaultCc = juce::jlimit(0, 127, control);
                        map.enabled.store(0, std::memory_order_relaxed);
                        map.inputChannel.store(0, std::memory_order_relaxed);
                        map.inputCc.store(defaultCc, std::memory_order_relaxed);
                        map.outputChannel.store(targetChannel, std::memory_order_relaxed);
                        map.outputCc.store(defaultCc, std::memory_order_relaxed);
                        map.minimum.store(0, std::memory_order_relaxed);
                        map.maximum.store(127, std::memory_order_relaxed);
                        map.value.store(0, std::memory_order_relaxed);
                        map.inputSet.store(0, std::memory_order_relaxed);
                        map.valueSet.store(0, std::memory_order_relaxed);
                    }
                }
            }
        }
    }

    hasSurfaceEditorSnapshot_ = false;
}

void GalahadMidiToolsProcessor::syncSurfaceEditorToSelectedMap() noexcept
{
    const SurfaceEditorSnapshot current{
        juce::jlimit(0, ControllerSlotCount - 1, parameterIntValue(surfaceEditorParameters_.controller, 0)),
        juce::jlimit(0, ControllerSurfaceControlCount - 1, parameterIntValue(surfaceEditorParameters_.control, 0)),
        juce::jlimit(0, ControllerLayerCount - 1, parameterIntValue(surfaceEditorParameters_.layer, 0)),
        juce::jlimit(0,
                     ControllerPatternCount - 1,
                     static_cast<int>(std::lround(valueOf(parameters_, galahad::plugin::ControllerPatternId)))),
        juce::jlimit(0,
                     ControllerTargetChannelCount - 1,
                     static_cast<int>(std::lround(valueOf(parameters_, galahad::plugin::ControllerTargetChannelId)))),
        parameterBoolValue(surfaceEditorParameters_.enabled, false) ? 1 : 0,
        juce::jlimit(0, 16, parameterIntValue(surfaceEditorParameters_.inputChannel, 0)),
        juce::jlimit(0, 127, parameterIntValue(surfaceEditorParameters_.inputCc, 0)),
        juce::jlimit(0, 15, parameterIntValue(surfaceEditorParameters_.outputChannel, 0)),
        juce::jlimit(0, 127, parameterIntValue(surfaceEditorParameters_.outputCc, 0)),
        juce::jlimit(0, 127, parameterIntValue(surfaceEditorParameters_.minimum, 0)),
        juce::jlimit(0, 127, parameterIntValue(surfaceEditorParameters_.maximum, 127)),
    };

    const bool selectionChanged = hasSurfaceEditorSnapshot_
        && (current.controller != lastSurfaceEditorSnapshot_.controller
            || current.control != lastSurfaceEditorSnapshot_.control
            || current.layer != lastSurfaceEditorSnapshot_.layer
            || current.pattern != lastSurfaceEditorSnapshot_.pattern
            || current.targetChannel != lastSurfaceEditorSnapshot_.targetChannel);

    const bool editChanged = hasSurfaceEditorSnapshot_
        && (current.enabled != lastSurfaceEditorSnapshot_.enabled
            || current.inputChannel != lastSurfaceEditorSnapshot_.inputChannel
            || current.inputCc != lastSurfaceEditorSnapshot_.inputCc
            || current.outputChannel != lastSurfaceEditorSnapshot_.outputChannel
            || current.outputCc != lastSurfaceEditorSnapshot_.outputCc
            || current.minimum != lastSurfaceEditorSnapshot_.minimum
            || current.maximum != lastSurfaceEditorSnapshot_.maximum);

    if (editChanged)
    {
        const int targetController = selectionChanged ? lastSurfaceEditorSnapshot_.controller : current.controller;
        const int targetControl = selectionChanged ? lastSurfaceEditorSnapshot_.control : current.control;
        const int targetLayer = selectionChanged ? lastSurfaceEditorSnapshot_.layer : current.layer;
        const int targetPattern = selectionChanged ? lastSurfaceEditorSnapshot_.pattern : current.pattern;
        const int targetChannel = selectionChanged ? lastSurfaceEditorSnapshot_.targetChannel : current.targetChannel;
        const int index = galahad::plugin::controllerSurfaceMapIndex(targetController,
                                                                     targetPattern,
                                                                     targetChannel,
                                                                     targetControl,
                                                                     targetLayer);
        auto& map = controllerSurfaceMaps_[static_cast<size_t>(index)];
        map.enabled.store(current.enabled, std::memory_order_relaxed);
        map.inputChannel.store(current.inputChannel, std::memory_order_relaxed);
        map.inputCc.store(current.inputCc, std::memory_order_relaxed);
        map.outputChannel.store(current.outputChannel, std::memory_order_relaxed);
        map.outputCc.store(current.outputCc, std::memory_order_relaxed);
        map.minimum.store(current.minimum, std::memory_order_relaxed);
        map.maximum.store(current.maximum, std::memory_order_relaxed);
        if (current.inputChannel != lastSurfaceEditorSnapshot_.inputChannel
            || current.inputCc != lastSurfaceEditorSnapshot_.inputCc)
            map.inputSet.store(1, std::memory_order_relaxed);
    }

    lastSurfaceEditorSnapshot_ = current;
    hasSurfaceEditorSnapshot_ = true;
}

void GalahadMidiToolsProcessor::syncAndLoadSurfaceEditorSelection()
{
    syncSurfaceEditorToSelectedMap();

    const int controller = juce::jlimit(0, ControllerSlotCount - 1, parameterIntValue(surfaceEditorParameters_.controller, 0));
    const int control = juce::jlimit(0, ControllerSurfaceControlCount - 1, parameterIntValue(surfaceEditorParameters_.control, 0));
    const int layer = juce::jlimit(0, ControllerLayerCount - 1, parameterIntValue(surfaceEditorParameters_.layer, 0));
    const int pattern = juce::jlimit(0,
                                    ControllerPatternCount - 1,
                                    static_cast<int>(std::lround(valueOf(parameters_, galahad::plugin::ControllerPatternId))));
    const int targetChannel = juce::jlimit(0,
                                          ControllerTargetChannelCount - 1,
                                          static_cast<int>(std::lround(valueOf(parameters_, galahad::plugin::ControllerTargetChannelId))));
    loadSurfaceEditorFromMap(controller, control, layer, pattern, targetChannel);
}

void GalahadMidiToolsProcessor::loadSurfaceEditorFromMap(int controller, int control, int layer, int pattern, int targetChannel)
{
    controller = juce::jlimit(0, ControllerSlotCount - 1, controller);
    control = juce::jlimit(0, ControllerSurfaceControlCount - 1, control);
    layer = juce::jlimit(0, ControllerLayerCount - 1, layer);
    pattern = juce::jlimit(0, ControllerPatternCount - 1, pattern);
    targetChannel = juce::jlimit(0, ControllerTargetChannelCount - 1, targetChannel);

    const int index = galahad::plugin::controllerSurfaceMapIndex(controller, pattern, targetChannel, control, layer);
    const auto& map = controllerSurfaceMaps_[static_cast<size_t>(index)];
    const SurfaceEditorSnapshot loaded{
        controller,
        control,
        layer,
        pattern,
        targetChannel,
        map.enabled.load(std::memory_order_relaxed),
        map.inputChannel.load(std::memory_order_relaxed),
        map.inputCc.load(std::memory_order_relaxed),
        map.outputChannel.load(std::memory_order_relaxed),
        map.outputCc.load(std::memory_order_relaxed),
        map.minimum.load(std::memory_order_relaxed),
        map.maximum.load(std::memory_order_relaxed),
    };

    setSurfaceEditorParameter(galahad::plugin::SurfaceEditEnabledId, static_cast<float>(loaded.enabled));
    setSurfaceEditorParameter(galahad::plugin::SurfaceEditInputChannelId, static_cast<float>(loaded.inputChannel));
    setSurfaceEditorParameter(galahad::plugin::SurfaceEditInputCcId, static_cast<float>(loaded.inputCc));
    setSurfaceEditorParameter(galahad::plugin::SurfaceEditOutputChannelId, static_cast<float>(loaded.outputChannel));
    setSurfaceEditorParameter(galahad::plugin::SurfaceEditOutputCcId, static_cast<float>(loaded.outputCc));
    setSurfaceEditorParameter(galahad::plugin::SurfaceEditMinimumId, static_cast<float>(loaded.minimum));
    setSurfaceEditorParameter(galahad::plugin::SurfaceEditMaximumId, static_cast<float>(loaded.maximum));

    lastSurfaceEditorSnapshot_ = loaded;
    hasSurfaceEditorSnapshot_ = true;
}

void GalahadMidiToolsProcessor::setSurfaceEditorParameter(const char* id, float value)
{
    if (auto* parameter = parameters_.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

GalahadMidiToolsProcessor::SurfaceMapSnapshot GalahadMidiToolsProcessor::surfaceMapSnapshot(int controller,
                                                                                           int control,
                                                                                           int layer,
                                                                                           int pattern,
                                                                                           int targetChannel) const noexcept
{
    controller = juce::jlimit(0, ControllerSlotCount - 1, controller);
    control = juce::jlimit(0, ControllerSurfaceControlCount - 1, control);
    layer = juce::jlimit(0, ControllerLayerCount - 1, layer);
    pattern = juce::jlimit(0, ControllerPatternCount - 1, pattern);
    targetChannel = juce::jlimit(0, ControllerTargetChannelCount - 1, targetChannel);

    const int index = galahad::plugin::controllerSurfaceMapIndex(controller, pattern, targetChannel, control, layer);
    const auto& map = controllerSurfaceMaps_[static_cast<size_t>(index)];
    return SurfaceMapSnapshot{
        map.enabled.load(std::memory_order_relaxed),
        map.inputChannel.load(std::memory_order_relaxed),
        map.inputCc.load(std::memory_order_relaxed),
        map.outputChannel.load(std::memory_order_relaxed),
        map.outputCc.load(std::memory_order_relaxed),
        map.minimum.load(std::memory_order_relaxed),
        map.maximum.load(std::memory_order_relaxed),
        map.value.load(std::memory_order_relaxed),
        map.inputSet.load(std::memory_order_relaxed),
        map.valueSet.load(std::memory_order_relaxed),
    };
}

void GalahadMidiToolsProcessor::setSurfaceMapSnapshot(int controller,
                                                      int control,
                                                      int layer,
                                                      int pattern,
                                                      int targetChannel,
                                                      const SurfaceMapSnapshot& snapshot) noexcept
{
    controller = juce::jlimit(0, ControllerSlotCount - 1, controller);
    control = juce::jlimit(0, ControllerSurfaceControlCount - 1, control);
    layer = juce::jlimit(0, ControllerLayerCount - 1, layer);
    pattern = juce::jlimit(0, ControllerPatternCount - 1, pattern);
    targetChannel = juce::jlimit(0, ControllerTargetChannelCount - 1, targetChannel);

    const int index = galahad::plugin::controllerSurfaceMapIndex(controller, pattern, targetChannel, control, layer);
    auto& map = controllerSurfaceMaps_[static_cast<size_t>(index)];
    map.enabled.store(juce::jlimit(0, 1, snapshot.enabled), std::memory_order_relaxed);
    map.inputChannel.store(juce::jlimit(0, 16, snapshot.inputChannel), std::memory_order_relaxed);
    map.inputCc.store(juce::jlimit(0, 127, snapshot.inputCc), std::memory_order_relaxed);
    map.outputChannel.store(juce::jlimit(0, 15, snapshot.outputChannel), std::memory_order_relaxed);
    map.outputCc.store(juce::jlimit(0, 127, snapshot.outputCc), std::memory_order_relaxed);
    map.minimum.store(juce::jlimit(0, 127, snapshot.minimum), std::memory_order_relaxed);
    map.maximum.store(juce::jlimit(0, 127, snapshot.maximum), std::memory_order_relaxed);
    map.value.store(juce::jlimit(0, 127, snapshot.value), std::memory_order_relaxed);
    map.inputSet.store(juce::jlimit(0, 1, snapshot.inputSet), std::memory_order_relaxed);
    map.valueSet.store(juce::jlimit(0, 1, snapshot.valueSet), std::memory_order_relaxed);
}

void GalahadMidiToolsProcessor::learnSurfaceMap(int controller,
                                                int control,
                                                int layer,
                                                int pattern,
                                                int targetChannel,
                                                const ControllerSnapshot& snapshot) noexcept
{
    if (snapshot.controller < 0)
        return;

    auto map = surfaceMapSnapshot(controller, control, layer, pattern, targetChannel);
    map.enabled = 1;
    map.inputChannel = juce::jlimit(1, 16, snapshot.channel);
    map.inputCc = juce::jlimit(0, 127, snapshot.controller);
    map.inputSet = 1;
    setSurfaceMapSnapshot(controller, control, layer, pattern, targetChannel, map);
}

GalahadMidiToolsProcessor::SurfacePerformanceContext GalahadMidiToolsProcessor::currentSurfacePerformanceContext() const noexcept
{
    return SurfacePerformanceContext{
        juce::jlimit(0,
                     ControllerLayerCount - 1,
                     static_cast<int>(std::lround(valueOf(parameters_, galahad::plugin::ControllerLayerId)))),
        juce::jlimit(0,
                     ControllerPatternCount - 1,
                     static_cast<int>(std::lround(valueOf(parameters_, galahad::plugin::ControllerPatternId)))),
        juce::jlimit(0,
                     ControllerTargetChannelCount - 1,
                     static_cast<int>(std::lround(valueOf(parameters_, galahad::plugin::ControllerTargetChannelId)))),
    };
}

void GalahadMidiToolsProcessor::appendSurfaceRecallEvents(const SurfacePerformanceContext& context,
                                                          std::span<galahad::MidiEvent> output,
                                                          size_t& outputCount) noexcept
{
    for (int controller = 0; controller < ControllerSlotCount; ++controller)
    {
        for (int control = 0; control < ControllerSurfaceControlCount; ++control)
        {
            const int index = galahad::plugin::controllerSurfaceMapIndex(controller,
                                                                         context.pattern,
                                                                         context.targetChannel,
                                                                         control,
                                                                         context.layer);
            const auto& map = controllerSurfaceMaps_[static_cast<size_t>(index)];
            if (map.enabled.load(std::memory_order_relaxed) == 0
                || map.valueSet.load(std::memory_order_relaxed) == 0
                || outputCount >= output.size())
                continue;

            const uint8_t outputChannel = static_cast<uint8_t>(juce::jlimit(0, 15, map.outputChannel.load(std::memory_order_relaxed)) + 1);
            const uint8_t outputCc = static_cast<uint8_t>(juce::jlimit(0, 127, map.outputCc.load(std::memory_order_relaxed)));
            const uint8_t value = static_cast<uint8_t>(juce::jlimit(0, 127, map.value.load(std::memory_order_relaxed)));
            const auto event = galahad::MidiEvent{ galahad::MidiEventType::ControlChange, outputChannel, outputCc, value, 0 };

            output[outputCount++] = event;
            recordControllerOutput(event, context.layer);
        }
    }
}

juce::ValueTree GalahadMidiToolsProcessor::createControllerSurfaceMapsState() const
{
    juce::ValueTree state{ SurfaceMapsStateId };

    for (int controller = 0; controller < ControllerSlotCount; ++controller)
    {
        for (int pattern = 0; pattern < ControllerPatternCount; ++pattern)
        {
            for (int targetChannel = 0; targetChannel < ControllerTargetChannelCount; ++targetChannel)
            {
                for (int control = 0; control < ControllerSurfaceControlCount; ++control)
                {
                    for (int layer = 0; layer < ControllerLayerCount; ++layer)
                    {
                        const int index = galahad::plugin::controllerSurfaceMapIndex(controller, pattern, targetChannel, control, layer);
                        const auto& map = controllerSurfaceMaps_[static_cast<size_t>(index)];
                        const int defaultCc = juce::jlimit(0, 127, control);
                        const int enabled = map.enabled.load(std::memory_order_relaxed);
                        const int inputChannel = map.inputChannel.load(std::memory_order_relaxed);
                        const int inputCc = map.inputCc.load(std::memory_order_relaxed);
                        const int outputChannel = map.outputChannel.load(std::memory_order_relaxed);
                        const int outputCc = map.outputCc.load(std::memory_order_relaxed);
                        const int minimum = map.minimum.load(std::memory_order_relaxed);
                        const int maximum = map.maximum.load(std::memory_order_relaxed);
                        const int value = map.value.load(std::memory_order_relaxed);
                        const int inputSet = map.inputSet.load(std::memory_order_relaxed);
                        const int valueSet = map.valueSet.load(std::memory_order_relaxed);

                        const bool isDefault = enabled == 0
                            && inputChannel == 0
                            && inputCc == defaultCc
                            && outputChannel == targetChannel
                            && outputCc == defaultCc
                            && minimum == 0
                            && maximum == 127
                            && value == 0
                            && inputSet == 0
                            && valueSet == 0;
                        if (isDefault)
                            continue;

                        juce::ValueTree child{ SurfaceMapStateId };
                        child.setProperty("controller", controller, nullptr);
                        child.setProperty("pattern", pattern, nullptr);
                        child.setProperty("targetChannel", targetChannel, nullptr);
                        child.setProperty("control", control, nullptr);
                        child.setProperty("layer", layer, nullptr);
                        child.setProperty("enabled", enabled, nullptr);
                        child.setProperty("inputChannel", inputChannel, nullptr);
                        child.setProperty("inputCc", inputCc, nullptr);
                        child.setProperty("outputChannel", outputChannel, nullptr);
                        child.setProperty("outputCc", outputCc, nullptr);
                        child.setProperty("minimum", minimum, nullptr);
                        child.setProperty("maximum", maximum, nullptr);
                        child.setProperty("value", value, nullptr);
                        child.setProperty("inputSet", inputSet, nullptr);
                        child.setProperty("valueSet", valueSet, nullptr);
                        state.addChild(child, -1, nullptr);
                    }
                }
            }
        }
    }

    return state;
}

void GalahadMidiToolsProcessor::restoreControllerSurfaceMapsState(const juce::ValueTree& state)
{
    initializeControllerSurfaceMaps();
    if (!state.isValid())
        return;

    for (int childIndex = 0; childIndex < state.getNumChildren(); ++childIndex)
    {
        const auto child = state.getChild(childIndex);
        if (!child.hasType(SurfaceMapStateId))
            continue;

        const int controller = juce::jlimit(0, ControllerSlotCount - 1, static_cast<int>(child.getProperty("controller", 0)));
        const int pattern = juce::jlimit(0, ControllerPatternCount - 1, static_cast<int>(child.getProperty("pattern", 0)));
        const int targetChannel = juce::jlimit(0,
                                              ControllerTargetChannelCount - 1,
                                              static_cast<int>(child.getProperty("targetChannel", 0)));
        const int control = juce::jlimit(0, ControllerSurfaceControlCount - 1, static_cast<int>(child.getProperty("control", 0)));
        const int layer = juce::jlimit(0, ControllerLayerCount - 1, static_cast<int>(child.getProperty("layer", 0)));
        const int index = galahad::plugin::controllerSurfaceMapIndex(controller, pattern, targetChannel, control, layer);
        auto& map = controllerSurfaceMaps_[static_cast<size_t>(index)];

        map.enabled.store(juce::jlimit(0, 1, static_cast<int>(child.getProperty("enabled", 0))), std::memory_order_relaxed);
        map.inputChannel.store(juce::jlimit(0, 16, static_cast<int>(child.getProperty("inputChannel", 0))), std::memory_order_relaxed);
        map.inputCc.store(juce::jlimit(0, 127, static_cast<int>(child.getProperty("inputCc", control))), std::memory_order_relaxed);
        map.outputChannel.store(juce::jlimit(0, 15, static_cast<int>(child.getProperty("outputChannel", targetChannel))), std::memory_order_relaxed);
        map.outputCc.store(juce::jlimit(0, 127, static_cast<int>(child.getProperty("outputCc", control))), std::memory_order_relaxed);
        map.minimum.store(juce::jlimit(0, 127, static_cast<int>(child.getProperty("minimum", 0))), std::memory_order_relaxed);
        map.maximum.store(juce::jlimit(0, 127, static_cast<int>(child.getProperty("maximum", 127))), std::memory_order_relaxed);
        map.value.store(juce::jlimit(0, 127, static_cast<int>(child.getProperty("value", 0))), std::memory_order_relaxed);
        map.inputSet.store(juce::jlimit(0,
                                        1,
                                        static_cast<int>(child.getProperty("inputSet",
                                                                           child.hasProperty("inputCc") ? 1 : 0))),
                           std::memory_order_relaxed);
        map.valueSet.store(juce::jlimit(0,
                                        1,
                                        static_cast<int>(child.getProperty("valueSet",
                                                                           child.hasProperty("value") ? 1 : 0))),
                           std::memory_order_relaxed);
    }
}

bool GalahadMidiToolsProcessor::appendControllerMappings(const galahad::MidiEvent& event,
                                                         int sourceControllerSlot,
                                                         std::span<galahad::MidiEvent> output,
                                                         size_t& outputCount) noexcept
{
    bool matched = false;

    auto appendMap = [this, &event, output, &outputCount](const ControllerMapParameters& map, int telemetrySlot) mutable {
        if (!parameterBoolValue(map.enabled, false))
            return false;

        const int inputChannel = juce::jlimit(0, 16, parameterIntValue(map.inputChannel, 0));
        const int inputCc = juce::jlimit(0, 127, parameterIntValue(map.inputCc, 0));

        if (inputCc != event.data1)
            return false;

        if (inputChannel != 0 && inputChannel != event.channel)
            return false;

        if (outputCount >= output.size())
            return false;

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
        recordControllerOutput(mapped, telemetrySlot);
        return true;
    };

    auto appendSurfaceMap = [this, &event, output, &outputCount](ControllerSurfaceMap& map, int telemetrySlot) mutable {
        if (map.enabled.load(std::memory_order_relaxed) == 0)
            return false;

        const int inputChannel = juce::jlimit(0, 16, map.inputChannel.load(std::memory_order_relaxed));
        const int inputCc = juce::jlimit(0, 127, map.inputCc.load(std::memory_order_relaxed));

        if (inputCc != event.data1)
            return false;

        if (inputChannel != 0 && inputChannel != event.channel)
            return false;

        if (outputCount >= output.size())
            return false;

        const int outputChannelIndex = juce::jlimit(0, 15, map.outputChannel.load(std::memory_order_relaxed));
        const uint8_t outputChannel = static_cast<uint8_t>(outputChannelIndex + 1);
        const uint8_t outputCc = static_cast<uint8_t>(juce::jlimit(0, 127, map.outputCc.load(std::memory_order_relaxed)));
        const auto mapped = galahad::MidiEvent{ galahad::MidiEventType::ControlChange,
                                                outputChannel,
                                                outputCc,
                                                scaleControllerValue(event.data2,
                                                                     map.minimum.load(std::memory_order_relaxed),
                                                                     map.maximum.load(std::memory_order_relaxed)),
                                                event.sampleOffset };

        output[outputCount++] = mapped;
        map.value.store(mapped.data2, std::memory_order_relaxed);
        map.valueSet.store(1, std::memory_order_relaxed);
        recordControllerOutput(mapped, telemetrySlot);
        return true;
    };

    for (int slot = 0; slot < ControllerMapSlotCount; ++slot)
        matched = appendMap(controllerMapParameters_[static_cast<size_t>(slot)], slot) || matched;

    if (sourceControllerSlot >= 0 && sourceControllerSlot < ControllerSlotCount)
    {
        const auto context = currentSurfacePerformanceContext();

        for (int control = 0; control < ControllerSurfaceControlCount; ++control)
        {
            const int index = galahad::plugin::controllerSurfaceMapIndex(sourceControllerSlot,
                                                                         context.pattern,
                                                                         context.targetChannel,
                                                                         control,
                                                                         context.layer);
            matched = appendSurfaceMap(controllerSurfaceMaps_[static_cast<size_t>(index)], context.layer) || matched;
        }
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
