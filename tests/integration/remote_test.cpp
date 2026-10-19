#include "../check.hpp"

#include "fs_util.hpp"
#include "remote_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
std::future<net::Received> receiveOne(net::Receiver &receiver)
{
    return std::async(std::launch::async, [&receiver]() {
        net::Received received;
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
    net::Receiver receiver;
    std::string error;
    CHECK(receiver.open(0, 0, 0, error));
    CHECK(receiver.fbiPort() != 0 && receiver.webPort() != 0);

    std::future<net::Received> pending = receiveOne(receiver);
    const int client = connectTo(receiver.fbiPort());
    CHECK(client >= 0);
    // What servefiles.py sends: a big-endian length and newline-separated URLs.
    const std::string payload =
        "http://192.168.1.2:8080/a.cia\nhttp://192.168.1.2:8080/b%20c.cia";
    std::string message(4, '\0');
    const uint32_t length = htonl(static_cast<uint32_t>(payload.size()));
    std::memcpy(&message[0], &length, 4);
    sendText(client, message + payload);

    net::Received received = pending.get();
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
    net::Receiver receiver;
    std::string error;
    CHECK(receiver.open(0, 0, 0, error));
    std::future<net::Received> pending = receiveOne(receiver);
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
    net::Receiver receiver;
    std::string error;
    CHECK(receiver.open(0, 0, 0, error));

    std::future<net::Received> page = receiveOne(receiver);
    int client = connectTo(receiver.webPort());
    sendText(client, "GET / HTTP/1.1\r\nHost: 3ds\r\n\r\n");
    std::string response = readAll(client);
    ::close(client);
    CHECK(response.rfind("HTTP/1.1 200 OK", 0) == 0);
    CHECK(response.find("<form method=\"post\" action=\"/send\"") !=
          std::string::npos);
    CHECK(page.get().links.empty());

    std::future<net::Received> post = receiveOne(receiver);
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
    const net::Received received = post.get();
    CHECK_EQ(received.links.size(), static_cast<size_t>(2));
    CHECK_EQ(received.links[0], std::string("https://example.com/a.cia"));
    CHECK_EQ(received.replySocket, -1);

    std::future<net::Received> after = receiveOne(receiver);
    client = connectTo(receiver.webPort());
    sendText(client, "GET /?sent=2 HTTP/1.1\r\n\r\n");
    response = readAll(client);
    ::close(client);
    CHECK(response.find("Sent 2 links") != std::string::npos);
    (void)after.get();

    std::future<net::Received> missing = receiveOne(receiver);
    client = connectTo(receiver.webPort());
    sendText(client, "GET /favicon.ico HTTP/1.1\r\n\r\n");
    response = readAll(client);
    ::close(client);
    CHECK(response.rfind("HTTP/1.1 404", 0) == 0);
    (void)missing.get();
}

TEST("remote server: a taken port falls back, a silent client times out")
{
    net::Receiver first;
    std::string error;
    CHECK(first.open(0, 0, 0, error));

    // A port that was free a moment ago, for the fallback.
    unsigned short spare = 0;
    {
        net::Receiver probe;
        CHECK(probe.open(0, 0, 0, error));
        spare = probe.webPort();
    }
    // FBI's port taken: the web page still works without it.
    net::Receiver noFbi;
    CHECK(noFbi.open(first.fbiPort(), 0, 0, error));
    CHECK_EQ(noFbi.fbiPort(), static_cast<unsigned short>(0));
    CHECK(noFbi.webPort() != 0);
    noFbi.close();

    net::Receiver second;
    CHECK(!second.open(0, first.webPort(), 0, error));
    CHECK(!error.empty());
    CHECK(second.open(0, first.webPort(), spare, error));
    CHECK_EQ(second.webPort(), spare);
    second.close();

    // A sender that connects and says nothing is dropped after five
    // seconds, and the receiver is free again.
    const int client = connectTo(first.fbiPort());
    const auto started = std::chrono::steady_clock::now();
    net::Received received;
    CHECK(!first.wait(1000, received));
    const auto took = std::chrono::steady_clock::now() - started;
    CHECK(took >= std::chrono::milliseconds(4500));
    CHECK(took < std::chrono::seconds(7));
    CHECK(received.links.empty());
    ::close(client);
}

namespace {

// Serves on a thread for the length of a test, keeping what arrives.
class Serving {
public:
    explicit Serving(net::Receiver &receiver) : receiver_(receiver)
    {
        thread_ = std::thread([this]() {
            while (!stop_.load()) {
                net::Received received;
                if (receiver_.wait(50, received)) {
                    std::lock_guard<std::mutex> hold(lock_);
                    arrived_.push_back(received);
                }
            }
        });
    }
    ~Serving()
    {
        stop_.store(true);
        thread_.join();
    }
    std::vector<net::Received> arrived()
    {
        std::lock_guard<std::mutex> hold(lock_);
        return arrived_;
    }

private:
    net::Receiver &receiver_;
    std::atomic<bool> stop_ {false};
    std::mutex lock_;
    std::vector<net::Received> arrived_;
    std::thread thread_;
};

std::string roundTrip(unsigned short port, const std::string &request)
{
    const int client = connectTo(port);
    sendText(client, request);
    const std::string response = readAll(client);
    ::close(client);
    return response;
}

std::string bodyOf(const std::string &response)
{
    const size_t end = response.find("\r\n\r\n");
    return end == std::string::npos ? "" : response.substr(end + 4);
}

std::string post(unsigned short port, const std::string &route,
                 const std::string &form)
{
    return roundTrip(port, "POST " + route +
                              " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                              "X-Requested-With: 3dsDL\r\nContent-Length: " +
                              std::to_string(form.size()) + "\r\n\r\n" + form);
}

std::string put(unsigned short port, const std::string &target,
                const std::string &body)
{
    return roundTrip(port, "PUT " + target +
                              " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                              "X-Requested-With: 3dsDL\r\nContent-Length: " +
                              std::to_string(body.size()) + "\r\n\r\n" + body);
}

std::string get(unsigned short port, const std::string &target,
                const std::string &host = "127.0.0.1")
{
    return roundTrip(port, "GET " + target + " HTTP/1.1\r\nHost: " + host +
                              "\r\n\r\n");
}

bool startsWith(const std::string &text, const std::string &prefix)
{
    return text.rfind(prefix, 0) == 0;
}

struct TempRoot {
    std::string path;
    TempRoot()
    {
        char pattern[] = "/tmp/3dsdl-files-XXXXXX";
        path = mkdtemp(pattern);
    }
    ~TempRoot() { std::system(("rm -rf '" + path + "'").c_str()); }
};

} // namespace

TEST("remote server: files go up, come back, move and go away")
{
    TempRoot root;
    std::mutex lock;
    std::vector<net::Transfer> finished;
    net::FileSharing sharing;
    sharing.root = root.path;
    sharing.freeSpace = []() { return static_cast<int64_t>(1) << 30; };
    sharing.progress = [&](const net::Transfer &transfer) {
        if (transfer.finished) {
            std::lock_guard<std::mutex> hold(lock);
            finished.push_back(transfer);
        }
    };
    net::Receiver receiver;
    std::string error;
    CHECK(receiver.open(0, 0, 0, error));
    receiver.shareFiles(sharing);
    Serving serving(receiver);
    const unsigned short port = receiver.webPort();

    std::string response = get(port, "/api/list?path=%2F");
    CHECK(startsWith(response, "HTTP/1.1 200"));
    CHECK(bodyOf(response).find("\"entries\":[]") != std::string::npos);
    CHECK(bodyOf(response).find("\"free\":1073741824") != std::string::npos);

    // Into a folder that is not there yet, then made.
    CHECK(startsWith(put(port, "/api/file?path=%2Fcia%2Fa.bin", "x"),
                     "HTTP/1.1 404"));
    CHECK(startsWith(post(port, "/api/folder", "path=%2Fcia"), "HTTP/1.1 200"));
    CHECK(fs::directoryExists(root.path + "/cia"));

    // Bigger than one SD card write, sent in pieces as a phone would.
    std::string contents(300 * 1024 + 7, '\0');
    for (size_t index = 0; index < contents.size(); ++index) {
        contents[index] = static_cast<char>(index * 31 + index / 977);
    }
    int client = connectTo(port);
    sendText(client, "PUT /api/file?path=%2Fcia%2Fa%20b.bin HTTP/1.1\r\n"
                     "Host: 127.0.0.1\r\nX-Requested-With: 3dsDL\r\n"
                     "Content-Length: " +
                         std::to_string(contents.size()) + "\r\n\r\n" +
                         contents.substr(0, 1000));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sendText(client, contents.substr(1000));
    response = readAll(client);
    ::close(client);
    CHECK(startsWith(response, "HTTP/1.1 200"));
    std::string stored;
    CHECK(fs::readFile(root.path + "/cia/a b.bin", stored));
    CHECK(stored == contents);
    CHECK(!fs::fileExists(root.path + "/cia/.3dsdl-upload.part"));
    {
        std::lock_guard<std::mutex> hold(lock);
        CHECK_EQ(finished.size(), static_cast<size_t>(1));
        CHECK(finished.back().ok && finished.back().upload);
        CHECK_EQ(finished.back().done, static_cast<uint64_t>(contents.size()));
    }

    // Replacing has to be asked for.
    CHECK(startsWith(put(port, "/api/file?path=%2Fcia%2Fa%20b.bin", "new"),
                     "HTTP/1.1 409"));
    CHECK(startsWith(put(port, "/api/file?path=%2Fcia%2Fa%20b.bin&replace=1", "new"),
                     "HTTP/1.1 200"));
    CHECK(fs::readFile(root.path + "/cia/a b.bin", stored) && stored == "new");

    response = get(port, "/api/file?path=%2Fcia%2Fa%20b.bin&download=1");
    CHECK(startsWith(response, "HTTP/1.1 200"));
    CHECK(bodyOf(response) == "new");
    CHECK(response.find("Content-Security-Policy: sandbox") != std::string::npos);
    CHECK(response.find("attachment; filename=\"a b.bin\"") != std::string::npos);

    response = get(port, "/api/list?path=%2Fcia");
    CHECK(bodyOf(response).find("{\"name\":\"a b.bin\",\"dir\":false,\"size\":3,") !=
          std::string::npos);

    CHECK(startsWith(post(port, "/api/move", "from=%2Fcia%2Fa+b.bin&to=%2Fgame.cia"),
                     "HTTP/1.1 200"));
    CHECK(fs::fileExists(root.path + "/game.cia"));
    CHECK(startsWith(post(port, "/api/move", "from=%2Fcia&to=%2Fcia%2Finside"),
                     "HTTP/1.1 409"));

    // The page asks the console to install; only real CIAs go through.
    response = post(port, "/api/install", "paths=%2Fgame.cia%0A%2Fmissing.cia%0A%2Fcia");
    CHECK(startsWith(response, "HTTP/1.1 200"));
    CHECK(bodyOf(response) == "{\"count\":1}");
    CHECK(startsWith(post(port, "/api/install", "paths=%2Fcia"), "HTTP/1.1 400"));

    CHECK(startsWith(post(port, "/api/delete", "path=%2Fcia"), "HTTP/1.1 200"));
    CHECK(!fs::directoryExists(root.path + "/cia"));
    CHECK(startsWith(post(port, "/api/delete", "path=%2F"), "HTTP/1.1 400"));

    const std::vector<net::Received> arrived = serving.arrived();
    CHECK_EQ(arrived.size(), static_cast<size_t>(1));
    if (!arrived.empty()) {
        CHECK_EQ(arrived[0].install.size(), static_cast<size_t>(1));
        CHECK_EQ(arrived[0].install[0], root.path + "/game.cia");
        CHECK(arrived[0].links.empty());
    }
}

TEST("remote server: the file API turns away what it should")
{
    TempRoot root;
    net::Access access = net::Access::Allowed;
    net::FileSharing sharing;
    sharing.root = root.path;
    sharing.access = [&](const std::string &from) {
        CHECK_EQ(from, std::string("127.0.0.1"));
        return access;
    };
    sharing.freeSpace = []() { return static_cast<int64_t>(10); };
    net::Receiver receiver;
    std::string error;
    CHECK(receiver.open(0, 0, 0, error));

    // Nothing is shared until the app says so.
    {
        Serving serving(receiver);
        CHECK(startsWith(get(receiver.webPort(), "/api/list?path=%2F"),
                         "HTTP/1.1 404"));
    }
    receiver.shareFiles(sharing);
    Serving serving(receiver);
    const unsigned short port = receiver.webPort();

    CHECK(startsWith(get(port, "/api/list?path=%2F..%2F"), "HTTP/1.1 400"));
    CHECK(startsWith(get(port, "/api/list?path=%2F", "attacker.example"),
                     "HTTP/1.1 403"));
    // A form on another site cannot add the header.
    CHECK(startsWith(roundTrip(port, "POST /api/delete HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                                    "Content-Length: 6\r\n\r\npath=x"),
                     "HTTP/1.1 403"));
    // More than the card can hold is refused before it is sent.
    CHECK(startsWith(put(port, "/api/file?path=%2Fbig.bin", std::string(11, 'x')),
                     "HTTP/1.1 507"));
    CHECK(startsWith(roundTrip(port, "PUT /api/file?path=%2Fa HTTP/1.1\r\n"
                                    "Host: 127.0.0.1\r\nX-Requested-With: 3dsDL\r\n"
                                    "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n"),
                     "HTTP/1.1 411"));

    // A sender that gives up halfway leaves nothing behind.
    int client = connectTo(port);
    sendText(client, "PUT /api/file?path=%2Fhalf.bin HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                     "X-Requested-With: 3dsDL\r\nContent-Length: 8\r\n\r\nabcd");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ::close(client);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(!fs::fileExists(root.path + "/half.bin"));
    CHECK(!fs::fileExists(root.path + "/.3dsdl-upload.part"));

    access = net::Access::Pending;
    std::string response = get(port, "/api/list?path=%2F");
    CHECK(startsWith(response, "HTTP/1.1 403"));
    CHECK(bodyOf(response).find("\"access\":\"pending\"") != std::string::npos);
    access = net::Access::Denied;
    CHECK(bodyOf(get(port, "/api/list?path=%2F")).find("\"access\":\"denied\"") !=
          std::string::npos);
    // Links never need permission.
    CHECK(startsWith(get(port, "/"), "HTTP/1.1 200"));
}

TEST("remote server: a silent spare connection does not hold up the next")
{
    net::Receiver receiver;
    std::string error;
    CHECK(receiver.open(0, 0, 0, error));
    Serving serving(receiver);
    const int spare = connectTo(receiver.webPort());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto started = std::chrono::steady_clock::now();
    const std::string response = get(receiver.webPort(), "/");
    CHECK(startsWith(response, "HTTP/1.1 200"));
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(2));
    ::close(spare);
}
