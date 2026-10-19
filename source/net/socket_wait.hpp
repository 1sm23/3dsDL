#pragma once

#include <poll.h>

// Waiting on sockets without stalling the rest of the app.
//
// On the 3DS every socket call in the process goes through one session with
// the network service, which handles one request at a time. A thread that
// sits in poll() for 250 ms holds up every other thread's sends, receives
// and accepts for as long, which made each web request take one or two
// seconds while the listener and the mDNS responder both waited. These
// helpers only ever ask with a zero timeout and sleep between asks, outside
// the service.
namespace net {

// poll() for up to timeoutMs, asking every stepMs. Returns what poll()
// returns: the number of ready sockets, 0 on timeout, -1 on error.
int waitSockets(pollfd *sockets, nfds_t count, int timeoutMs, int stepMs);

// Makes a socket's calls return at once instead of waiting.
void setNonBlocking(int socketFd);

} // namespace net
