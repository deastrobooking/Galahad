#include "galahad/OscCodec.h"

#include <arpa/inet.h>
#include <cstring>

namespace galahad::osc
{
namespace
{
size_t paddedSize(size_t size)
{
    return ((size + 4) / 4) * 4;
}

void appendPaddedString(std::vector<uint8_t>& out, const std::string& s)
{
    const size_t start = out.size();
    out.resize(start + paddedSize(s.size() + 1), 0);
    std::memcpy(out.data() + start, s.c_str(), s.size());
}

std::optional<std::string> readPaddedString(const uint8_t* bytes, size_t length, size_t& offset)
{
    if (offset >= length)
        return std::nullopt;

    size_t cursor = offset;
    while (cursor < length && bytes[cursor] != '\0')
        ++cursor;

    if (cursor >= length)
        return std::nullopt;

    std::string result(reinterpret_cast<const char*>(bytes + offset), cursor - offset);
    offset = offset + paddedSize(result.size() + 1);

    if (offset > length)
        return std::nullopt;

    return result;
}

void appendInt32(std::vector<uint8_t>& out, int32_t value)
{
    uint32_t network = htonl(static_cast<uint32_t>(value));
    auto* ptr = reinterpret_cast<const uint8_t*>(&network);
    out.insert(out.end(), ptr, ptr + 4);
}

void appendFloat(std::vector<uint8_t>& out, float value)
{
    static_assert(sizeof(float) == sizeof(uint32_t), "Unexpected float size");
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    bits = htonl(bits);
    auto* ptr = reinterpret_cast<const uint8_t*>(&bits);
    out.insert(out.end(), ptr, ptr + 4);
}

std::optional<int32_t> readInt32(const uint8_t* bytes, size_t length, size_t& offset)
{
    if (offset + 4 > length)
        return std::nullopt;

    uint32_t network;
    std::memcpy(&network, bytes + offset, sizeof(network));
    offset += 4;
    return static_cast<int32_t>(ntohl(network));
}

std::optional<float> readFloat(const uint8_t* bytes, size_t length, size_t& offset)
{
    if (offset + 4 > length)
        return std::nullopt;

    uint32_t network;
    std::memcpy(&network, bytes + offset, sizeof(network));
    offset += 4;

    uint32_t host = ntohl(network);
    float value;
    std::memcpy(&value, &host, sizeof(value));
    return value;
}
} // namespace

std::vector<uint8_t> encodeMessage(const Command& command)
{
    std::vector<uint8_t> output;
    appendPaddedString(output, command.address);

    std::string typeTags = ",";
    for (const auto& argument : command.arguments)
    {
        if (std::holds_alternative<int32_t>(argument))
            typeTags.push_back('i');
        else if (std::holds_alternative<float>(argument))
            typeTags.push_back('f');
        else if (std::holds_alternative<std::string>(argument))
            typeTags.push_back('s');
    }

    appendPaddedString(output, typeTags);

    for (const auto& argument : command.arguments)
    {
        if (const auto* i = std::get_if<int32_t>(&argument))
            appendInt32(output, *i);
        else if (const auto* f = std::get_if<float>(&argument))
            appendFloat(output, *f);
        else if (const auto* s = std::get_if<std::string>(&argument))
            appendPaddedString(output, *s);
    }

    return output;
}

std::optional<Command> decodeMessage(const uint8_t* bytes, size_t length)
{
    if (bytes == nullptr || length == 0)
        return std::nullopt;

    size_t offset = 0;
    auto address = readPaddedString(bytes, length, offset);
    auto tags = readPaddedString(bytes, length, offset);

    if (!address || !tags || tags->empty() || tags->front() != ',')
        return std::nullopt;

    Command command;
    command.address = *address;

    for (size_t i = 1; i < tags->size(); ++i)
    {
        switch ((*tags)[i])
        {
            case 'i':
            {
                auto value = readInt32(bytes, length, offset);
                if (!value)
                    return std::nullopt;
                command.arguments.emplace_back(*value);
                break;
            }
            case 'f':
            {
                auto value = readFloat(bytes, length, offset);
                if (!value)
                    return std::nullopt;
                command.arguments.emplace_back(*value);
                break;
            }
            case 's':
            {
                auto value = readPaddedString(bytes, length, offset);
                if (!value)
                    return std::nullopt;
                command.arguments.emplace_back(*value);
                break;
            }
            default:
                return std::nullopt;
        }
    }

    return command;
}
} // namespace galahad::osc
