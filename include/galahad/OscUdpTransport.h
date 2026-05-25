#pragma once

#include "galahad/Transport.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace galahad
{
class OscUdpTransport : public ITransport
{
public:
    OscUdpTransport(std::string remoteHost, int remotePort, int localPort);
    ~OscUdpTransport() override;

    bool start() override;
    void stop() override;

    bool send(const Command& command) override;
    void setHandler(CommandHandler handler) override;

private:
    void receiveLoop();

    std::string remoteHost_;
    int remotePort_;
    int localPort_;

    int socketFd_ = -1;
    std::thread receiverThread_;
    std::atomic<bool> running_{ false };

    std::mutex handlerMutex_;
    CommandHandler handler_;
};
} // namespace galahad
