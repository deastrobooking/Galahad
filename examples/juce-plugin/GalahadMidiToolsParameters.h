#pragma once

#include <juce_core/juce_core.h>

namespace galahad::plugin
{
inline constexpr int ControllerMapSlotCount = 4;

inline constexpr const char* HardwareCaptureId = "hardwareCapture";
inline constexpr const char* MapThruId = "mapThru";
inline constexpr const char* MapEnabledSuffix = "Enabled";
inline constexpr const char* MapInputChannelSuffix = "InCh";
inline constexpr const char* MapInputCcSuffix = "InCc";
inline constexpr const char* MapOutputChannelSuffix = "OutCh";
inline constexpr const char* MapOutputCcSuffix = "OutCc";
inline constexpr const char* MapMinimumSuffix = "Min";
inline constexpr const char* MapMaximumSuffix = "Max";

inline juce::String controllerMapParameterId(int slot, const char* suffix)
{
    return "map" + juce::String(slot + 1) + suffix;
}

inline juce::StringArray inputChannelChoices()
{
    juce::StringArray choices{ "Omni" };
    for (int channel = 1; channel <= 16; ++channel)
        choices.add("Ch " + juce::String(channel));
    return choices;
}

inline juce::StringArray outputChannelChoices()
{
    juce::StringArray choices;
    for (int channel = 1; channel <= 16; ++channel)
        choices.add("Ch " + juce::String(channel));
    return choices;
}
} // namespace galahad::plugin
