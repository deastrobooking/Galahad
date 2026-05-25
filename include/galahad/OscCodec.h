#pragma once

#include "galahad/Command.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace galahad::osc
{
std::vector<uint8_t> encodeMessage(const Command& command);
std::optional<Command> decodeMessage(const uint8_t* bytes, size_t length);
} // namespace galahad::osc
