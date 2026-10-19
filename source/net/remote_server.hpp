#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Listens on the local network: FBI's remote install protocol for links, and
// a web page (see core/remote.hpp) for links and for the files on the SD
// card. One connection at a time, which is all a person sending things
// needs. The 3DS app runs it on a worker thread; the host tests run it
// against real sockets.
namespace net {

// What a sender handed over for the app to act on.
struct Received {
    std::vector<std::string> links;
    // CIAs on the SD card (local paths) the page asked to install.
    std::vector<std::string> install;
    std::string from; // the sender's address
    // FBI's tools wait on this connection for one byte, sent by
    // finishReceived once the links are done. -1 for the web page.
    int replySocket = -1;
};

// Tells an FBI tool the links are done, so it can stop serving its files,
// and closes the connection.
void finishReceived(Received &received);

// A file going to or coming from the SD card through the page.
struct Transfer {
    std::string name;
    std::string from;
    bool upload = true; // false when the page is downloading it
    uint64_t done = 0;
    uint64_t total = 0;
    bool finished = false; // the last report for this file
    bool ok = false;       // with finished: the whole file went through
};

enum class Access { Allowed, Pending, Denied };

// How the page reaches the SD card. Without a root, the file API answers
// 404 and only links can be sent.
struct FileSharing {
    std::string root; // "sdmc:"
    // Whether the device at an address may use the files. Pending makes the
    // page wait and ask again. Unset allows everyone.
    std::function<Access(const std::string &from)> access;
    std::function<int64_t()> freeSpace; // bytes, or -1 when unknown
    // Called a few times a second during a transfer, on the server's thread.
    std::function<void(const Transfer &)> progress;
    int socketBufferSize = 0; // asked for on each connection when not 0
};

class Receiver {
public:
    Receiver() = default;
    ~Receiver();
    Receiver(const Receiver &) = delete;
    Receiver &operator=(const Receiver &) = delete;

    // Port 0 picks a free port (for tests). When webPort is taken,
    // fallbackWebPort is tried. Fails, with a reason, only when the web page
    // cannot listen; fbiPort() is 0 when FBI's port was taken.
    bool open(unsigned short fbiPort, unsigned short webPort,
              unsigned short fallbackWebPort, std::string &error);
    void close();
    unsigned short fbiPort() const { return fbiPort_; }
    unsigned short webPort() const { return webPort_; }

    // Set after open() and before wait() is first called.
    void shareFiles(const FileSharing &sharing);

    // Waits up to timeoutMs for a connection and serves it. True when it
    // brought something for the app, which is in out.
    bool wait(int timeoutMs, Received &out);

private:
    bool serveFbi(int client, const std::string &from, Received &out);
    bool serveWeb(int client, const std::string &from, Received &out);

    int fbiSocket_ = -1;
    int webSocket_ = -1;
    unsigned short fbiPort_ = 0;
    unsigned short webPort_ = 0;
    FileSharing sharing_;
};

} // namespace net
