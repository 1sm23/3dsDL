#include "../check.hpp"

#include "socket_wait.hpp"

#include <chrono>

namespace {

// A stand-in for the network service: ready after readyAfter asks, and
// remembering the longest timeout it was handed.
int asks = 0;
int readyAfter = 0;
int longestTimeout = 0;

int fakePoll(pollfd *, nfds_t, int timeoutMs)
{
    ++asks;
    if (timeoutMs > longestTimeout) {
        longestTimeout = timeoutMs;
    }
    return readyAfter > 0 && asks >= readyAfter ? 1 : 0;
}

void reset(int ready)
{
    asks = 0;
    readyAfter = ready;
    longestTimeout = 0;
}

long long elapsedMs(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
}

} // namespace

TEST("socket wait: never hands the service a timeout")
{
    pollfd socket {};
    reset(4);
    CHECK_EQ(net::pollInSteps(fakePoll, &socket, 1, 1000), 1);
    CHECK_EQ(asks, 4);
    CHECK_EQ(longestTimeout, 0);

    reset(3);
    CHECK_EQ(net::pollInSteps(fakePoll, &socket, 1, -1), 1);
    CHECK_EQ(asks, 3);
    CHECK_EQ(longestTimeout, 0);

    reset(0);
    CHECK_EQ(net::pollInSteps(fakePoll, &socket, 1, 0), 0);
    CHECK_EQ(asks, 1);
}

TEST("socket wait: times out, asking less often as the wait goes on")
{
    pollfd socket {};
    reset(0);
    const auto start = std::chrono::steady_clock::now();
    CHECK_EQ(net::pollInSteps(fakePoll, &socket, 1, 120), 0);
    CHECK(elapsedMs(start) >= 120);
    CHECK_EQ(longestTimeout, 0);
    // Asking every millisecond would be about 120 asks; the steps make it
    // about 20.
    CHECK(asks <= 30);
}

TEST("socket wait: nothing to watch sleeps for the timeout")
{
    reset(0);
    const auto start = std::chrono::steady_clock::now();
    CHECK_EQ(net::pollInSteps(fakePoll, nullptr, 0, 30), 0);
    CHECK(elapsedMs(start) >= 30);
    CHECK_EQ(asks, 0);
}
