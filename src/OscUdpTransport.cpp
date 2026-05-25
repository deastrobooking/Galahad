#include "galahad/OscUdpTransport.h"

#include "galahad/OscCodec.h"

#include <array>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace galahad
{
namespace
{
void closeSocket(int fd)
{
#if defined(_WIN32)
    closesocket(fd);
#else
    close(fd);
#endif
}
} // namespace

OscUdpTransport::OscUdpTransport(std::string remoteHost, int remotePort, int localPort)
    : remoteHost_(std::move(remoteHost)), remotePort_(remotePort), localPort_(localPort)
{
}

OscUdpTransport::~OscUdpTransport()
{
    stop();
}

bool OscUdpTransport::start()
{
    if (running_.load())
        return true;

#if defined(_WIN32)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return false;
#endif

    socketFd_ = static_cast<int>(socket(AF_INET, SOCK_DGRAM, 0));
    if (socketFd_ < 0)
        return false;

    sockaddr_in localAddr{};
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(static_cast<uint16_t>(localPort_));
    localAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(socketFd_, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) < 0)
    {
        closeSocket(socketFd_);
        socketFd_ = -1;
        return false;
    }

    running_.store(true);
    receiverThread_ = std::thread([this] { receiveLoop(); });

    return true;
}

void OscUdpTransport::stop()
{
    if (!running_.exchange(false))
        return;

    if (socketFd_ >= 0)
    {
#if !defined(_WIN32)
        shutdown(socketFd_, SHUT_RDWR);
#endif
        closeSocket(socketFd_);
        socketFd_ = -1;
    }

    if (receiverThread_.joinable())
        receiverThread_.join();

#if defined(_WIN32)
    WSACleanup();
#endif
}

bool OscUdpTransport::send(const Command& command)
{
    if (socketFd_ < 0)
        return false;

    sockaddr_in remoteAddr{};
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(static_cast<uint16_t>(remotePort_));

    if (inet_pton(AF_INET, remoteHost_.c_str(), &remoteAddr.sin_addr) != 1)
        return false;

    const auto payload = osc::encodeMessage(command);
    const auto sent = ::sendto(socketFd_,
                               reinterpret_cast<const char*>(payload.data()),
                               static_cast<int>(payload.size()),
                               0,
                               reinterpret_cast<const sockaddr*>(&remoteAddr),
                               sizeof(remoteAddr));

    return sent == static_cast<int>(payload.size());
}

void OscUdpTransport::setHandler(CommandHandler handler)
{
    std::lock_guard<std::mutex> lock(handlerMutex_);
    handler_ = std::move(handler);
}

void OscUdpTransport::receiveLoop()
{
    std::array<uint8_t, 4096> buffer{};

    while (running_.load())
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socketFd_, &readSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        const int ready = select(socketFd_ + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready <= 0)
            continue;

        sockaddr_in fromAddr{};
        socklen_t fromLen = sizeof(fromAddr);

        const int readBytes = recvfrom(socketFd_,
                                       reinterpret_cast<char*>(buffer.data()),
                                       static_cast<int>(buffer.size()),
                                       0,
                                       reinterpret_cast<sockaddr*>(&fromAddr),
                                       &fromLen);

        if (readBytes <= 0)
            continue;

        auto message = osc::decodeMessage(buffer.data(), static_cast<size_t>(readBytes));
        if (!message)
            continue;

        CommandHandler callback;
        {
            std::lock_guard<std::mutex> lock(handlerMutex_);
            callback = handler_;
        }

        if (callback)
            callback(*message);
    }
}
} // namespace galahad
