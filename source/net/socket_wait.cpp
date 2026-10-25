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

using Clock = std::chrono::steady_clock;

void sleepMs(int milliseconds)
{
#ifdef __3DS__
    svcSleepThread(static_cast<s64>(milliseconds) * 1000000);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#endif
}

long long msSince(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               Clock::now() - start)
        .count();
}

} // namespace

int waitSockets(pollfd *sockets, nfds_t count, int timeoutMs, int stepMs)
{
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

int pollInSteps(int (*ask)(pollfd *, nfds_t, int), pollfd *sockets,
                nfds_t count, int timeoutMs)
{
    if (count == 0 && timeoutMs > 0) {
        // How libcurl sleeps when it has no socket to watch. libctru turns
        // it down with EINVAL, which made those waits spin instead.
        sleepMs(timeoutMs);
        return 0;
    }
    if (count == 0 || timeoutMs == 0) {
        return ask(sockets, count, timeoutMs);
    }
    const Clock::time_point start = Clock::now();
    while (true) {
        const int ready = ask(sockets, count, 0);
        if (ready != 0) {
            return ready;
        }
        const long long waited = msSince(start);
        if (timeoutMs > 0 && waited >= timeoutMs) {
            return 0;
        }
        long long step = waited < 5 ? 1 : waited < 50 ? 5 : 20;
        if (timeoutMs > 0 && step > timeoutMs - waited) {
            step = timeoutMs - waited;
        }
        sleepMs(static_cast<int>(step));
    }
}

void setNonBlocking(int socketFd)
{
    const int flags = fcntl(socketFd, F_GETFL, 0);
    fcntl(socketFd, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
}

} // namespace net

#ifdef __3DS__
extern "C" {

int __real_poll(pollfd *sockets, nfds_t count, int timeoutMs);

int __wrap_poll(pollfd *sockets, nfds_t count, int timeoutMs)
{
    return net::pollInSteps(__real_poll, sockets, count, timeoutMs);
}

} // extern "C"
#endif
