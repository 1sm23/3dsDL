#pragma once

#include <string>
#include <vector>

// Listens for links on the local network: FBI's remote install protocol and
// a small web page (see core/remote.hpp). One connection at a time, which is
// all a person sending links needs. The 3DS app runs it on a worker thread;
// the host tests run it against real sockets.
namespace net {

struct ReceivedLinks {
    std::vector<std::string> links;
    std::string from; // the sender's address
    // FBI's tools wait on this connection for one byte, sent by
    // finishReceived once the links are done. -1 for the web page.
    int replySocket = -1;
};

// Tells an FBI tool the links are done, so it can stop serving its files,
// and closes the connection.
void finishReceived(ReceivedLinks &received);

class LinkReceiver {
public:
    LinkReceiver() = default;
    ~LinkReceiver();
    LinkReceiver(const LinkReceiver &) = delete;
    LinkReceiver &operator=(const LinkReceiver &) = delete;

    // Port 0 picks a free port (for tests). When webPort is taken,
    // fallbackWebPort is tried. Fails, with a reason, only when the web page
    // cannot listen; fbiPort() is 0 when FBI's port was taken.
    bool open(unsigned short fbiPort, unsigned short webPort,
              unsigned short fallbackWebPort, std::string &error);
    void close();
    unsigned short fbiPort() const { return fbiPort_; }
    unsigned short webPort() const { return webPort_; }

    // Waits up to timeoutMs for a connection and serves it. True when it
    // brought links, which are in out.
    bool wait(int timeoutMs, ReceivedLinks &out);

private:
    bool serveFbi(int client, const std::string &from, ReceivedLinks &out);
    bool serveWeb(int client, const std::string &from, ReceivedLinks &out);

    int fbiSocket_ = -1;
    int webSocket_ = -1;
    unsigned short fbiPort_ = 0;
    unsigned short webPort_ = 0;
};

} // namespace net
