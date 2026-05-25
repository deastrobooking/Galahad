#include "galahad/MidiRouting.h"

#include <algorithm>

namespace galahad
{
namespace
{
constexpr MidiRouteRule DisabledRule{ false, 0, 1, 0, 0, true, true };
}

void MidiMergerRouter::clearRules() noexcept
{
    for (auto& next : rules_)
        next = DisabledRule;
}

bool MidiMergerRouter::setRule(size_t index, const MidiRouteRule& rule) noexcept
{
    if (index >= MaxRules)
        return false;

    rules_[index] = rule;
    rules_[index].inputChannel = std::clamp<uint8_t>(rule.inputChannel, 0, 16);
    rules_[index].outputChannel = std::clamp<uint8_t>(rule.outputChannel, 1, 16);
    rules_[index].transpose = std::clamp(rule.transpose, -48, 48);
    rules_[index].velocityOffset = std::clamp(rule.velocityOffset, -127, 127);
    return true;
}

const MidiRouteRule& MidiMergerRouter::rule(size_t index) const noexcept
{
    if (index >= MaxRules)
        return DisabledRule;

    return rules_[index];
}

size_t MidiMergerRouter::route(std::span<const MidiEvent> input, std::span<MidiEvent> output) const noexcept
{
    size_t count = 0;

    for (const auto& event : input)
    {
        bool emitted = false;
        for (const auto& nextRule : rules_)
        {
            if (!ruleMatches(nextRule, event))
                continue;

            if (count >= output.size())
                return count;

            output[count++] = applyRule(event, nextRule);
            emitted = true;
        }

        if (!emitted && count < output.size())
            output[count++] = event;
    }

    return count;
}

size_t MidiMergerRouter::merge(std::span<const MidiEvent> a, std::span<const MidiEvent> b, std::span<MidiEvent> output) const noexcept
{
    size_t ai = 0;
    size_t bi = 0;
    size_t count = 0;

    while (count < output.size() && (ai < a.size() || bi < b.size()))
    {
        if (bi >= b.size() || (ai < a.size() && compareEvents(a[ai], b[bi]) <= 0))
            output[count++] = a[ai++];
        else
            output[count++] = b[bi++];
    }

    return count;
}

MidiEvent MidiMergerRouter::applyRule(const MidiEvent& event, const MidiRouteRule& rule) noexcept
{
    MidiEvent routed = event;
    routed.channel = rule.outputChannel;

    if (event.type == MidiEventType::NoteOn || event.type == MidiEventType::NoteOff)
    {
        routed.data1 = clampMidi(static_cast<int>(event.data1) + rule.transpose);
        routed.data2 = clampMidi(static_cast<int>(event.data2) + rule.velocityOffset);
    }

    return routed;
}

bool MidiMergerRouter::ruleMatches(const MidiRouteRule& rule, const MidiEvent& event) noexcept
{
    if (!rule.enabled)
        return false;

    if (rule.inputChannel != 0 && rule.inputChannel != event.channel)
        return false;

    if ((event.type == MidiEventType::NoteOn || event.type == MidiEventType::NoteOff) && !rule.passNotes)
        return false;

    if (event.type == MidiEventType::ControlChange && !rule.passControlChanges)
        return false;

    return true;
}

uint8_t MidiMergerRouter::clampMidi(int value) noexcept
{
    return static_cast<uint8_t>(std::clamp(value, 0, 127));
}

int MidiMergerRouter::compareEvents(const MidiEvent& lhs, const MidiEvent& rhs) noexcept
{
    if (lhs.sampleOffset < rhs.sampleOffset)
        return -1;
    if (lhs.sampleOffset > rhs.sampleOffset)
        return 1;
    return 0;
}
} // namespace galahad
