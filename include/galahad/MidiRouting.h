#pragma once

#include "galahad/MidiEvent.h"

#include <cstddef>
#include <span>

namespace galahad
{
class MidiMergerRouter
{
public:
    static constexpr size_t MaxRules = 16;

    void clearRules() noexcept;
    bool setRule(size_t index, const MidiRouteRule& rule) noexcept;
    const MidiRouteRule& rule(size_t index) const noexcept;

    size_t route(std::span<const MidiEvent> input, std::span<MidiEvent> output) const noexcept;

    /// Merge two event streams into @p output in sample-offset order.
    /// @pre Both @p a and @p b must already be sorted in ascending sampleOffset order.
    ///      Passing unsorted spans produces silently incorrect output.
    size_t merge(std::span<const MidiEvent> a, std::span<const MidiEvent> b, std::span<MidiEvent> output) const noexcept;

private:
    static MidiEvent applyRule(const MidiEvent& event, const MidiRouteRule& rule) noexcept;
    static bool ruleMatches(const MidiRouteRule& rule, const MidiEvent& event) noexcept;
    static uint8_t clampMidi(int value) noexcept;
    static int compareEvents(const MidiEvent& lhs, const MidiEvent& rhs) noexcept;

    MidiRouteRule rules_[MaxRules]{};
};
} // namespace galahad
