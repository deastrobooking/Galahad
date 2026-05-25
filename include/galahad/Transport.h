#pragma once

#include "galahad/Command.h"

#include <functional>

namespace galahad
{
using CommandHandler = std::function<void(const Command&)>;

class ITransport
{
public:
    virtual ~ITransport() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;

    virtual bool send(const Command& command) = 0;
    virtual void setHandler(CommandHandler handler) = 0;
};
} // namespace galahad
