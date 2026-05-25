#pragma once

#include <juce_core/juce_core.h>

namespace galahad::plugin
{
inline constexpr int ControllerMapSlotCount = 4;
inline constexpr int ControllerSlotCount = 8;
inline constexpr int ControllerLayerCount = 4;
inline constexpr int ControllerFaderCount = 8;
inline constexpr int ControllerButtonCount = 15;
inline constexpr int ControllerSurfaceControlCount = ControllerFaderCount + ControllerButtonCount;
inline constexpr int ControllerSurfaceMapSlotCount =
    ControllerSlotCount * ControllerSurfaceControlCount * ControllerLayerCount;

inline constexpr const char* HardwareCaptureId = "hardwareCapture";
inline constexpr const char* ControllerLayerId = "controllerLayer";
inline constexpr const char* SurfaceEditControllerId = "surfaceEditController";
inline constexpr const char* SurfaceEditControlId = "surfaceEditControl";
inline constexpr const char* SurfaceEditLayerId = "surfaceEditLayer";
inline constexpr const char* SurfaceEditEnabledId = "surfaceEditEnabled";
inline constexpr const char* SurfaceEditInputChannelId = "surfaceEditInCh";
inline constexpr const char* SurfaceEditInputCcId = "surfaceEditInCc";
inline constexpr const char* SurfaceEditOutputChannelId = "surfaceEditOutCh";
inline constexpr const char* SurfaceEditOutputCcId = "surfaceEditOutCc";
inline constexpr const char* SurfaceEditMinimumId = "surfaceEditMin";
inline constexpr const char* SurfaceEditMaximumId = "surfaceEditMax";
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

inline int controllerSurfaceMapIndex(int controllerSlot, int control, int layer)
{
    return ((controllerSlot * ControllerSurfaceControlCount) + control) * ControllerLayerCount + layer;
}

inline bool isControllerFaderControl(int control)
{
    return control >= 0 && control < ControllerFaderCount;
}

inline juce::String controllerSurfaceControlName(int control)
{
    if (isControllerFaderControl(control))
        return "Fader " + juce::String(control + 1);

    return "Button " + juce::String(control - ControllerFaderCount + 1);
}

inline juce::String controllerSurfaceMapParameterId(int controllerSlot, int control, int layer, const char* suffix)
{
    const auto controlPrefix = isControllerFaderControl(control)
        ? "F" + juce::String(control + 1)
        : "B" + juce::String(control - ControllerFaderCount + 1);

    return "ctl" + juce::String(controllerSlot + 1)
        + controlPrefix
        + "Map" + juce::String(layer + 1)
        + suffix;
}

inline juce::StringArray controllerSlotChoices()
{
    juce::StringArray choices;
    for (int slot = 1; slot <= ControllerSlotCount; ++slot)
        choices.add("Controller " + juce::String(slot));
    return choices;
}

inline juce::StringArray controllerLayerChoices()
{
    juce::StringArray choices;
    for (int layer = 1; layer <= ControllerLayerCount; ++layer)
        choices.add("Map " + juce::String(layer));
    return choices;
}

inline juce::StringArray controllerSurfaceControlChoices()
{
    juce::StringArray choices;
    for (int control = 0; control < ControllerSurfaceControlCount; ++control)
        choices.add(controllerSurfaceControlName(control));
    return choices;
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
