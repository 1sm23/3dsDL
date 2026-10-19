#pragma once

#include <cstdint>
#include <string>

// Answers multicast DNS questions for "<name>.local" with the console's
// address, so a phone or computer can open http://3ds.local/ instead of
// typing the IP address. The 3DS app runs it on its own thread while Receive
// is open; the host tests run it on loopback without the multicast group.
namespace net {

class MdnsResponder {
public:
    MdnsResponder() = default;
    ~MdnsResponder();
    MdnsResponder(const MdnsResponder &) = delete;
    MdnsResponder &operator=(const MdnsResponder &) = delete;

    // Listens on port (mdns::Port, or 0 for a free one in tests) for name,
    // e.g. "3ds", at address (network byte order). multicast joins the mDNS
    // group, which the real thing needs and loopback tests cannot use.
    bool open(const std::string &name, uint32_t address, unsigned short port,
              bool multicast, std::string &error);
    void close();
    unsigned short port() const { return port_; }

    // Asks the network whether the name is taken, and moves on to "3ds-2",
    // "3ds-3" and so on while it is. Takes about a second per name tried.
    // Then announces the name. Only with multicast.
    void claim();
    // "3ds.local", or "3ds-2.local" after claim found "3ds" taken.
    std::string hostname() const;

    // Answers questions that arrive within timeoutMs.
    void serve(int timeoutMs);

private:
    void send(const void *data, size_t size, uint32_t address,
              unsigned short port);

    int socket_ = -1;
    std::string base_;
    int suffix_ = 1;
    uint32_t address_ = 0;
    unsigned short port_ = 0;
    bool multicast_ = false;
};

} // namespace net
