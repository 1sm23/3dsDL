#include "../check.hpp"

#include "mdns.hpp"
#include "mdns_responder.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <future>
#include <vector>

// The responder on loopback, asked the way a one-shot resolver asks (from a
// port other than 5353), which needs no multicast.

namespace {

std::vector<uint8_t> ask(unsigned short port, const std::string &name)
{
    const int socketFd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in to {};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::vector<uint8_t> query = mdns::probe(name);
    query[0] = 0xbe; // an ID that has to come back
    query[1] = 0xef;
    query[query.size() - 2] = 0; // no unicast bit, as a plain resolver
    sendto(socketFd, query.data(), query.size(), 0,
           reinterpret_cast<sockaddr *>(&to), sizeof(to));
    std::vector<uint8_t> reply(1500);
    pollfd waiting {socketFd, POLLIN, 0};
    ssize_t got = 0;
    if (poll(&waiting, 1, 1500) > 0) {
        got = recv(socketFd, reply.data(), reply.size(), 0);
    }
    ::close(socketFd);
    reply.resize(got > 0 ? static_cast<size_t>(got) : 0);
    return reply;
}

} // namespace

TEST("mdns responder: answers for its name and nothing else")
{
    in_addr address {};
    inet_aton("192.168.7.9", &address);
    net::MdnsResponder responder;
    std::string error;
    CHECK(responder.open("3ds", address.s_addr, 0, false, error));
    CHECK(responder.port() != 0);
    CHECK_EQ(responder.hostname(), std::string("3ds.local"));
    responder.claim(); // nothing to do without multicast
    CHECK_EQ(responder.hostname(), std::string("3ds.local"));

    std::future<void> serving = std::async(std::launch::async, [&]() {
        responder.serve(2000);
        responder.serve(1000);
    });
    const std::vector<uint8_t> reply = ask(responder.port(), "3DS.local");
    CHECK(reply.size() > 16);
    if (reply.size() > 16) {
        CHECK_EQ(reply[0], 0xbe);
        CHECK_EQ(reply[1], 0xef);
        CHECK(mdns::claims(reply.data(), reply.size(), "3ds.local"));
        CHECK(std::vector<uint8_t>(reply.end() - 4, reply.end()) ==
              std::vector<uint8_t>({192, 168, 7, 9}));
    }
    CHECK(ask(responder.port(), "other.local").empty());
    serving.get();
}
