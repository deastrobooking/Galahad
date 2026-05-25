#pragma once

#include <cstdint>

namespace galahad
{
enum class MidiEventType : uint8_t
{
    NoteOff,
    NoteOn,
    ControlChange
};

struct MidiEvent
{
    MidiEventType type{ MidiEventType::NoteOff };
    uint8_t channel{ 1 };
    uint8_t data1{ 0 };
    uint8_t data2{ 0 };
    int sampleOffset{ 0 };
};

struct MidiRouteRule
{
    bool enabled{ true };
    uint8_t inputChannel{ 0 };
    uint8_t outputChannel{ 1 };
    int transpose{ 0 };
    int velocityOffset{ 0 };
    bool passNotes{ true };
    bool passControlChanges{ true };
};
} // namespace galahad
