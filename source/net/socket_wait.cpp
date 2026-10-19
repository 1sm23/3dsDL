#include "socket_wait.hpp"

#include <fcntl.h>

#include <chrono>

#ifdef __3DS__
#include <3ds.h>
#else
#include <thread>
#endif

namespace net {

namespace {

void sleepMs(int milliseconds)
{
#ifdef __3DS__
    svcSleepThread(static_cast<s64>(milliseconds) * 1000000);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#endif
}

} // namespace

int waitSockets(pollfd *sockets, nfds_t count, int timeoutMs, int stepMs)
{
    using Clock = std::chrono::steady_clock;
    const Clock::time_point until =
        Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        const int ready = poll(sockets, count, 0);
        if (ready != 0) {
            return ready;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              until - Clock::now())
                              .count();
        if (left <= 0) {
            return 0;
        }
        sleepMs(static_cast<int>(left < stepMs ? left : stepMs));
    }
}

void setNonBlocking(int socketFd)
{
    const int flags = fcntl(socketFd, F_GETFL, 0);
    fcntl(socketFd, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
}

} // namespace net
