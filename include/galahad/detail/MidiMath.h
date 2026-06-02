#pragma once

#include <algorithm>
#include <cstdint>

// Internal helpers shared across galahad MIDI modules.
// Not part of the public API — include paths may change without notice.

namespace galahad::detail
{
/// Clamp an integer to the valid MIDI value range [0, 127].
[[nodiscard]] inline uint8_t clampMidi(int value) noexcept
{
    return static_cast<uint8_t>(std::clamp(value, 0, 127));
}
} // namespace galahad::detail
