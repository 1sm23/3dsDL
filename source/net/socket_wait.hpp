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

// poll() with ask doing the asking, which only ever gets a zero timeout.
// Asks every millisecond at first, so an answer that comes quickly is seen
// quickly, then every 5 and every 20, so a long wait does not keep the
// system core busy: the network service shares it with the SD card. A
// negative timeoutMs waits until a socket is ready.
//
// The console links every poll() through this (-Wl,--wrap=poll in the
// Makefile), so libcurl's waits while it connects, shakes hands and
// downloads do not hold up the service either.
int pollInSteps(int (*ask)(pollfd *, nfds_t, int), pollfd *sockets,
                nfds_t count, int timeoutMs);

// Makes a socket's calls return at once instead of waiting.
void setNonBlocking(int socketFd);

} // namespace net
