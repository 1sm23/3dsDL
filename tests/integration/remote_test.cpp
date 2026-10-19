#include "../check.hpp"

#include "remote_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <string>
#include <thread>

// Runs the link receiver on loopback against FBI-style and browser-style
// clients.

namespace {

int connectTo(unsigned short port)
{
    const int socketFd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socketFd, reinterpret_cast<sockaddr *>(&address),
                sizeof(address)) != 0) {
        ::close(socketFd);
        return -1;
    }
    return socketFd;
}

void sendText(int socketFd, const std::string &data)
{
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t wrote = send(socketFd, data.data() + sent, data.size() - sent, 0);
        if (wrote <= 0) {
            return;
        }
        sent += static_cast<size_t>(wrote);
    }
}

std::string readAll(int socketFd, int timeoutMs = 3000)
{
    std::string data;
    char buffer[4096];
    while (true) {
        pollfd waiting {socketFd, POLLIN, 0};
        if (poll(&waiting, 1, timeoutMs) <= 0) {
            break;
        }
        const ssize_t got = recv(socketFd, buffer, sizeof(buffer), 0);
        if (got <= 0) {
            break;
        }
        data.append(buffer, static_cast<size_t>(got));
    }
    return data;
}

// Serves until a batch arrives or a few seconds pass.
std::future<net::ReceivedLinks> receiveOne(net::LinkReceiver &receiver)
{
    return std::async(std::launch::async, [&receiver]() {
        net::ReceivedLinks received;
        for (int round = 0; round < 40; ++round) {
            if (receiver.wait(100, received)) {
                break;
            }
        }
        return received;
    });
}

} // namespace

TEST("remote server: FBI's protocol, with the reply byte after the work")
{
    net::LinkReceiver receiver;
    std::string error;
    CHECK(receiver.open(0, 0, 0, error));
    CHECK(receiver.fbiPort() != 0 && receiver.webPort() != 0);

    std::future<net::ReceivedLinks> pending = receiveOne(receiver);
    const int client = connectTo(receiver.fbiPort());
    CHECK(client >= 0);
    // What servefiles.py sends: a big-endian length and newline-separated URLs.
    const std::string payload =
        "http://192.168.1.2:8080/a.cia\nhttp://192.168.1.2:8080/b%20c.cia";
    std::string message(4, '\0');
    const uint32_t length = htonl(static_cast<uint32_t>(payload.size()));
    std::memcpy(&message[0], &length, 4);
    sendText(client, message + payload);

    net::ReceivedLinks received = pending.get();
    CHECK_EQ(received.links.size(), static_cast<size_t>(2));
    CHECK_EQ(received.links[1], std::string("http://192.168.1.2:8080/b%20c.cia"));
    CHECK_EQ(received.from, std::string("127.0.0.1"));
    CHECK(received.replySocket >= 0);

    // The sender is still waiting: nothing has come back yet.
    pollfd waiting {client, POLLIN, 0};
    CHECK_EQ(poll(&waiting, 1, 200), 0);
    net::finishReceived(received);
    CHECK_EQ(received.replySocket, -1);
    const std::string reply = readAll(client, 2000);
    CHECK_EQ(reply.size(), static_cast<size_t>(1));
    ::close(client);
}

TEST("remote server: an FBI payload without links is answered at once")
{
    net::LinkReceiver receiver;
    std::string error;
    CHECK(receiver.open(0, 0, 0, error));
    std::future<net::ReceivedLinks> pending = receiveOne(receiver);
    const int client = connectTo(receiver.fbiPort());
    const std::string payload = "not a link";
    std::string message(4, '\0');
    const uint32_t length = htonl(static_cast<uint32_t>(payload.size()));
    std::memcpy(&message[0], &length, 4);
    sendText(client, message + payload);
    CHECK_EQ(readAll(client, 2000).size(), static_cast<size_t>(1));
    CHECK(pending.get().links.empty());
    ::close(client);
}

TEST("remote server: the web page, a form post and a redirect back")
{
    net::LinkReceiver receiver;
    std::string error;
    CHECK(receiver.open(0, 0, 0, error));

    std::future<net::ReceivedLinks> page = receiveOne(receiver);
    int client = connectTo(receiver.webPort());
    sendText(client, "GET / HTTP/1.1\r\nHost: 3ds\r\n\r\n");
    std::string response = readAll(client);
    ::close(client);
    CHECK(response.rfind("HTTP/1.1 200 OK", 0) == 0);
    CHECK(response.find("<form method=\"post\" action=\"/send\">") !=
          std::string::npos);
    CHECK(page.get().links.empty());

    std::future<net::ReceivedLinks> post = receiveOne(receiver);
    client = connectTo(receiver.webPort());
    const std::string body =
        "links=https%3A%2F%2Fexample.com%2Fa.cia%0D%0Ahttps%3A%2F%2Fexample.com%2Fb.zip";
    // Sent in two parts, as a slow phone might.
    sendText(client, "POST /send HTTP/1.1\r\nHost: 3ds\r\nContent-Length: " +
                         std::to_string(body.size()) + "\r\n\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    sendText(client, body);
    response = readAll(client);
    ::close(client);
    CHECK(response.rfind("HTTP/1.1 303 See Other", 0) == 0);
    CHECK(response.find("Location: /?sent=2") != std::string::npos);
    const net::ReceivedLinks received = post.get();
    CHECK_EQ(received.links.size(), static_cast<size_t>(2));
    CHECK_EQ(received.links[0], std::string("https://example.com/a.cia"));
    CHECK_EQ(received.replySocket, -1);

    std::future<net::ReceivedLinks> after = receiveOne(receiver);
    client = connectTo(receiver.webPort());
    sendText(client, "GET /?sent=2 HTTP/1.1\r\n\r\n");
    response = readAll(client);
    ::close(client);
    CHECK(response.find("Sent 2 links") != std::string::npos);
    (void)after.get();

    std::future<net::ReceivedLinks> missing = receiveOne(receiver);
    client = connectTo(receiver.webPort());
    sendText(client, "GET /favicon.ico HTTP/1.1\r\n\r\n");
    response = readAll(client);
    ::close(client);
    CHECK(response.rfind("HTTP/1.1 404", 0) == 0);
    (void)missing.get();
}

TEST("remote server: a taken port falls back, a silent client times out")
{
    net::LinkReceiver first;
    std::string error;
    CHECK(first.open(0, 0, 0, error));

    // A port that was free a moment ago, for the fallback.
    unsigned short spare = 0;
    {
        net::LinkReceiver probe;
        CHECK(probe.open(0, 0, 0, error));
        spare = probe.webPort();
    }
    // FBI's port taken: the web page still works without it.
    net::LinkReceiver noFbi;
    CHECK(noFbi.open(first.fbiPort(), 0, 0, error));
    CHECK_EQ(noFbi.fbiPort(), static_cast<unsigned short>(0));
    CHECK(noFbi.webPort() != 0);
    noFbi.close();

    net::LinkReceiver second;
    CHECK(!second.open(0, first.webPort(), 0, error));
    CHECK(!error.empty());
    CHECK(second.open(0, first.webPort(), spare, error));
    CHECK_EQ(second.webPort(), spare);
    second.close();

    // A sender that connects and says nothing is dropped after five
    // seconds, and the receiver is free again.
    const int client = connectTo(first.fbiPort());
    const auto started = std::chrono::steady_clock::now();
    net::ReceivedLinks received;
    CHECK(!first.wait(1000, received));
    const auto took = std::chrono::steady_clock::now() - started;
    CHECK(took >= std::chrono::milliseconds(4500));
    CHECK(took < std::chrono::seconds(7));
    CHECK(received.links.empty());
    ::close(client);
}
