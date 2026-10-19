#include "remote_server.hpp"

#include "remote.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

namespace net {

namespace {

// How long a sender gets to finish sending once it has connected.
constexpr int ReadTimeoutMs = 5000;

void closeSocket(int &socketFd)
{
    if (socketFd >= 0) {
        ::close(socketFd);
        socketFd = -1;
    }
}

void quietPipe(int socketFd)
{
#ifdef SO_NOSIGPIPE
    const int on = 1;
    setsockopt(socketFd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    (void)socketFd;
#endif
}

int sendFlags()
{
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

bool listenOn(unsigned short port, int &socketFd, unsigned short &bound,
              std::string &error)
{
    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0) {
        error = "Could not open a network socket.";
        return false;
    }
    const int on = 1;
    setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(socketFd, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) != 0 ||
        listen(socketFd, 4) != 0) {
        error = "Port " + std::to_string(port) + " is in use.";
        closeSocket(socketFd);
        return false;
    }
    socklen_t length = sizeof(address);
    if (getsockname(socketFd, reinterpret_cast<sockaddr *>(&address),
                    &length) == 0) {
        bound = ntohs(address.sin_port);
    } else {
        bound = port;
    }
    return true;
}

using Clock = std::chrono::steady_clock;

// Reads up to size bytes, waiting no later than deadline. Returns bytes
// read, 0 at the end of the stream, -1 on error or timeout.
ssize_t readSome(int socketFd, void *buffer, size_t size, Clock::time_point deadline)
{
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                          deadline - Clock::now())
                          .count();
    if (left <= 0) {
        return -1;
    }
    pollfd waiting {socketFd, POLLIN, 0};
    if (poll(&waiting, 1, static_cast<int>(left)) <= 0) {
        return -1;
    }
    const ssize_t got = recv(socketFd, buffer, size, 0);
    return got < 0 ? -1 : got;
}

bool readExactly(int socketFd, void *buffer, size_t size,
                 Clock::time_point deadline)
{
    unsigned char *bytes = static_cast<unsigned char *>(buffer);
    size_t done = 0;
    while (done < size) {
        const ssize_t got = readSome(socketFd, bytes + done, size - done,
                                     deadline);
        if (got <= 0) {
            return false;
        }
        done += static_cast<size_t>(got);
    }
    return true;
}

void sendAll(int socketFd, const std::string &data)
{
    size_t sent = 0;
    int tries = 0;
    while (sent < data.size() && tries < 200) {
        const ssize_t wrote = send(socketFd, data.data() + sent,
                                   data.size() - sent, sendFlags());
        if (wrote > 0) {
            sent += static_cast<size_t>(wrote);
            continue;
        }
        if (wrote < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
            errno != EINTR) {
            return;
        }
        pollfd waiting {socketFd, POLLOUT, 0};
        poll(&waiting, 1, 50);
        ++tries;
    }
}

} // namespace

void finishReceived(ReceivedLinks &received)
{
    if (received.replySocket >= 0) {
        const char done = 1;
        send(received.replySocket, &done, 1, sendFlags());
        closeSocket(received.replySocket);
    }
}

LinkReceiver::~LinkReceiver()
{
    close();
}

bool LinkReceiver::open(unsigned short fbiPort, unsigned short webPort,
                        unsigned short fallbackWebPort, std::string &error)
{
    close();
    // FBI's port is a bonus: without it the web page still works.
    std::string fbiError;
    if (!listenOn(fbiPort, fbiSocket_, fbiPort_, fbiError)) {
        fbiPort_ = 0;
    }
    std::string webError;
    if (!listenOn(webPort, webSocket_, webPort_, webError) &&
        (fallbackWebPort == 0 ||
         !listenOn(fallbackWebPort, webSocket_, webPort_, webError))) {
        error = webError;
        close();
        return false;
    }
    return true;
}

void LinkReceiver::close()
{
    closeSocket(fbiSocket_);
    closeSocket(webSocket_);
    fbiPort_ = 0;
    webPort_ = 0;
}

bool LinkReceiver::wait(int timeoutMs, ReceivedLinks &out)
{
    out = ReceivedLinks();
    if (webSocket_ < 0) {
        return false;
    }
    pollfd listeners[2] = {{webSocket_, POLLIN, 0}, {fbiSocket_, POLLIN, 0}};
    const nfds_t count = fbiSocket_ >= 0 ? 2 : 1;
    if (poll(listeners, count, timeoutMs) <= 0) {
        return false;
    }
    for (nfds_t index = 0; index < count; ++index) {
        if ((listeners[index].revents & POLLIN) == 0) {
            continue;
        }
        sockaddr_in peer {};
        socklen_t length = sizeof(peer);
        int client = accept(listeners[index].fd,
                            reinterpret_cast<sockaddr *>(&peer), &length);
        if (client < 0) {
            continue;
        }
        quietPipe(client);
        const std::string from = inet_ntoa(peer.sin_addr);
        const bool got = index == 0 ? serveWeb(client, from, out)
                                    : serveFbi(client, from, out);
        if (got) {
            return true;
        }
    }
    return false;
}

bool LinkReceiver::serveFbi(int client, const std::string &from,
                            ReceivedLinks &out)
{
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds(ReadTimeoutMs);
    unsigned char lengthBytes[4];
    if (!readExactly(client, lengthBytes, sizeof(lengthBytes), deadline)) {
        closeSocket(client);
        return false;
    }
    const size_t length = remote::payloadLength(lengthBytes);
    if (length == 0 || length > remote::MaxPayload) {
        closeSocket(client);
        return false;
    }
    std::string payload(length, '\0');
    if (!readExactly(client, &payload[0], length, deadline)) {
        closeSocket(client);
        return false;
    }
    out.links = remote::extractLinks(payload);
    out.from = from;
    out.replySocket = client;
    if (out.links.empty()) {
        // Nothing to download, so the sender may go at once.
        finishReceived(out);
        return false;
    }
    return true;
}

bool LinkReceiver::serveWeb(int client, const std::string &from,
                            ReceivedLinks &out)
{
    std::string data;
    remote::HttpRequest request;
    remote::Parse state = remote::Parse::Incomplete;
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds(ReadTimeoutMs);
    char buffer[4096];
    while (state == remote::Parse::Incomplete) {
        const ssize_t got = readSome(client, buffer, sizeof(buffer), deadline);
        if (got <= 0) {
            break;
        }
        data.append(buffer, static_cast<size_t>(got));
        state = remote::parseRequest(data, request);
    }
    if (state != remote::Parse::Done) {
        if (state == remote::Parse::Bad) {
            sendAll(client, remote::httpResponse(400, "text/plain",
                                                 "Bad request\n"));
        }
        closeSocket(client);
        return false;
    }

    const std::string html = "text/html; charset=utf-8";
    bool got = false;
    if (request.path == "/" && request.method == "GET") {
        const std::string sent = remote::formValue(request.query, "sent");
        const std::string message =
            sent.empty() ? ""
            : sent == "0" ? "No links were found in what you sent."
            : sent == "1" ? "Sent 1 link. It is downloading on the 3DS."
                          : "Sent " + sent +
                                " links. They are downloading on the 3DS.";
        sendAll(client, remote::httpResponse(200, html,
                                             remote::page(message, sent != "0")));
    } else if (request.path == "/send" &&
               (request.method == "POST" || request.method == "GET")) {
        const std::string &form =
            request.method == "POST" ? request.body : request.query;
        out.links = remote::extractLinks(remote::formValue(form, "links"));
        out.from = from;
        got = !out.links.empty();
        // Back to the form, so reloading the page does not send again.
        std::string response = "HTTP/1.1 303 See Other\r\nLocation: /?sent=" +
                               std::to_string(out.links.size()) +
                               "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        sendAll(client, response);
    } else if (request.method != "GET" && request.method != "POST") {
        sendAll(client, remote::httpResponse(405, "text/plain",
                                             "Method not allowed\n"));
    } else {
        sendAll(client, remote::httpResponse(404, "text/plain", "Not found\n"));
    }
    closeSocket(client);
    return got;
}

} // namespace net
