#include "remote_server.hpp"

#include "file_ops.hpp"
#include "file_types.hpp"
#include "fs_util.hpp"
#include "remote.hpp"
#include "socket_wait.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace net {

namespace {

// How long a sender gets to finish sending once it has connected.
constexpr int ReadTimeoutMs = 5000;
// A file transfer may take minutes, but gives up after this long without a
// byte going either way.
constexpr int IdleTimeoutMs = 30000;
// Browsers open spare connections that may never send anything. One that is
// still silent after this long gives way to the next one waiting.
constexpr int SpareConnectionMs = 300;
// Bytes gathered before each write to the SD card, which is much faster at
// a few large writes than at many small ones.
constexpr size_t WriteChunk = 256 * 1024;
// How often a wait asks whether a socket is ready: often while a request
// is being served, less often while nobody is connected.
constexpr int ActiveStepMs = 1;
constexpr int IdleStepMs = 5;
constexpr size_t ReadChunk = 64 * 1024;
constexpr int ProgressEveryMs = 200;

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
// read, 0 at the end of the stream, -1 on error or timeout. It tries the
// read first, so a busy connection costs one call per read, not two.
ssize_t readSome(int socketFd, void *buffer, size_t size, Clock::time_point deadline)
{
    while (true) {
        const ssize_t got = recv(socketFd, buffer, size, MSG_DONTWAIT);
        if (got >= 0) {
            return got;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return -1;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - Clock::now())
                              .count();
        if (left <= 0) {
            return -1;
        }
        pollfd waiting {socketFd, POLLIN, 0};
        if (waitSockets(&waiting, 1, static_cast<int>(left), ActiveStepMs) < 0) {
            return -1;
        }
    }
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

// Sends everything, giving up when the other side takes nothing for
// idleMs.
bool sendBytes(int socketFd, const char *data, size_t size, int idleMs)
{
    size_t sent = 0;
    while (sent < size) {
        const ssize_t wrote = send(socketFd, data + sent, size - sent,
                                   sendFlags() | MSG_DONTWAIT);
        if (wrote > 0) {
            sent += static_cast<size_t>(wrote);
            continue;
        }
        if (wrote < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
            errno != EINTR) {
            return false;
        }
        pollfd waiting {socketFd, POLLOUT, 0};
        if (waitSockets(&waiting, 1, idleMs, ActiveStepMs) <= 0) {
            return false;
        }
    }
    return true;
}

void sendAll(int socketFd, const std::string &data)
{
    sendBytes(socketFd, data.data(), data.size(), ReadTimeoutMs);
}

// Waits for a connection's first bytes. False when nothing comes before
// deadline, or when it stays silent while another connection is waiting.
bool firstBytes(int client, int listener, Clock::time_point deadline)
{
    const Clock::time_point started = Clock::now();
    while (true) {
        const Clock::time_point now = Clock::now();
        if (now >= deadline) {
            return false;
        }
        const int left = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                                  now)
                .count());
        pollfd waiting[2] = {{client, POLLIN, 0}, {listener, POLLIN, 0}};
        if (waitSockets(waiting, 2, std::min(left, 100), ActiveStepMs) < 0) {
            return false;
        }
        if (waiting[0].revents != 0) {
            return true;
        }
        if ((waiting[1].revents & POLLIN) != 0 &&
            Clock::now() - started >=
                std::chrono::milliseconds(SpareConnectionMs)) {
            return false;
        }
    }
}

void sendJson(int client, const std::string &json)
{
    sendAll(client, remote::httpResponse(200, "application/json", json));
}

// Reports a transfer to FileSharing::progress a few times a second.
class Reporter {
public:
    Reporter(const FileSharing &sharing, const std::string &name,
             const std::string &from, bool upload, uint64_t total)
        : sharing_(sharing)
    {
        transfer_.name = name;
        transfer_.from = from;
        transfer_.upload = upload;
        transfer_.total = total;
        report();
    }

    void advance(uint64_t done)
    {
        transfer_.done = done;
        if (Clock::now() - last_ >=
            std::chrono::milliseconds(ProgressEveryMs)) {
            report();
        }
    }

    void finish(bool ok)
    {
        transfer_.finished = true;
        transfer_.ok = ok;
        report();
    }

private:
    void report()
    {
        last_ = Clock::now();
        if (sharing_.progress) {
            sharing_.progress(transfer_);
        }
    }

    const FileSharing &sharing_;
    Transfer transfer_;
    Clock::time_point last_;
};

class NoCancel : public fileops::Observer {
public:
    bool cancelRequested() override { return false; }
};

// The folder that holds path: "sdmc:/a/b" -> "sdmc:/a", "sdmc:/a" ->
// "sdmc:/".
std::string folderOf(const std::string &path)
{
    const size_t slash = path.rfind('/');
    const std::string folder = path.substr(0, slash);
    return folder.find('/') == std::string::npos ? folder + "/" : folder;
}

bool isRoot(const FileSharing &sharing, const std::string &path)
{
    return path == sharing.root + "/";
}

// Reads the rest of a small body into request.body.
bool readBody(int client, std::string &data, size_t bodyStart,
              remote::HttpRequest &request, Clock::time_point deadline)
{
    if (request.contentLength > 64 * 1024) {
        sendAll(client, remote::jsonError(413, "That request is too large."));
        return false;
    }
    const size_t end = bodyStart + static_cast<size_t>(request.contentLength);
    char buffer[4096];
    while (data.size() < end) {
        const ssize_t got = readSome(client, buffer, sizeof(buffer), deadline);
        if (got <= 0) {
            return false;
        }
        data.append(buffer, static_cast<size_t>(got));
    }
    request.body = data.substr(bodyStart, end - bodyStart);
    return true;
}

void listFolder(int client, const FileSharing &sharing,
                const std::string &webPath, const std::string &local)
{
    bool ok = false;
    const std::vector<fs::Entry> entries = fs::listEntries(local, &ok);
    if (!ok) {
        sendAll(client, remote::jsonError(404, "That folder does not exist."));
        return;
    }
    const int64_t free = sharing.freeSpace ? sharing.freeSpace() : -1;
    std::string json = "{\"path\":" + remote::jsonString(webPath) +
                       ",\"free\":" + std::to_string(free) + ",\"entries\":[";
    for (size_t index = 0; index < entries.size(); ++index) {
        const fs::Entry &entry = entries[index];
        json += std::string(index > 0 ? "," : "") +
                "{\"name\":" + remote::jsonString(entry.name) +
                ",\"dir\":" + (entry.folder ? "true" : "false") +
                ",\"size\":" + std::to_string(entry.size) +
                ",\"time\":" + std::to_string(entry.modified) + "}";
    }
    sendJson(client, json + "]}");
}

void sendFile(int client, const FileSharing &sharing, const std::string &from,
              const std::string &local, bool asAttachment)
{
    const int64_t size = fs::fileSize(local);
    FILE *file = size < 0 || fileops::isFolder(local)
                     ? nullptr
                     : std::fopen(local.c_str(), "rb");
    if (file == nullptr) {
        sendAll(client, remote::jsonError(404, "That file does not exist."));
        return;
    }
    const std::string name = fileops::name(local);
    // A sandboxed, never-sniffed response cannot run script with the page's
    // access to the API, whatever is in the file.
    const std::string head =
        "HTTP/1.1 200 OK\r\nContent-Type: " + std::string(remote::mimeType(name)) +
        "\r\nContent-Length: " + std::to_string(size) +
        "\r\nContent-Disposition: " +
        remote::contentDisposition(name, asAttachment) +
        "\r\nX-Content-Type-Options: nosniff"
        "\r\nContent-Security-Policy: sandbox"
        "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    Reporter reporter(sharing, name, from, false, static_cast<uint64_t>(size));
    bool ok = sendBytes(client, head.data(), head.size(), IdleTimeoutMs);
    std::vector<char> buffer(ReadChunk);
    uint64_t done = 0;
    while (ok && done < static_cast<uint64_t>(size)) {
        const size_t got = std::fread(buffer.data(), 1, buffer.size(), file);
        if (got == 0) {
            ok = false;
            break;
        }
        ok = sendBytes(client, buffer.data(), got, IdleTimeoutMs);
        done += got;
        reporter.advance(done);
    }
    std::fclose(file);
    reporter.finish(ok);
}

// Streams a PUT body into local, through a hidden part file that only
// takes the real name once every byte is on the card.
void receiveFile(int client, const FileSharing &sharing,
                 const std::string &from, const remote::HttpRequest &request,
                 const std::string &data, size_t bodyStart,
                 const std::string &local)
{
    const std::string name = fileops::name(local);
    const std::string folder = folderOf(local);
    const bool replace = remote::formValue(request.query, "replace") == "1";
    if (request.headers.count("transfer-encoding") != 0 ||
        request.headers.count("content-length") == 0) {
        sendAll(client, remote::jsonError(411, "Send the file with its size."));
        return;
    }
    if (!fs::directoryExists(folder)) {
        sendAll(client, remote::jsonError(404, "The folder for " + name +
                                                   " does not exist."));
        return;
    }
    if (fileops::isFolder(local)) {
        sendAll(client,
                remote::jsonError(409, "A folder is already called " + name + "."));
        return;
    }
    const bool exists = fileops::exists(local);
    if (exists && !replace) {
        sendAll(client, remote::jsonError(409, name + " already exists."));
        return;
    }
    const uint64_t total = request.contentLength;
    if (sharing.freeSpace) {
        const int64_t free = sharing.freeSpace();
        const uint64_t freed =
            exists ? static_cast<uint64_t>(std::max<int64_t>(0, fs::fileSize(local)))
                   : 0;
        if (free >= 0 && total > static_cast<uint64_t>(free) + freed) {
            sendAll(client, remote::jsonError(
                                507, "The SD card does not have room for " +
                                         name + "."));
            return;
        }
    }

    const std::string part =
        (folder.back() == '/' ? folder : folder + "/") + ".3dsdl-upload.part";
    FILE *file = std::fopen(part.c_str(), "wb");
    if (file == nullptr) {
        sendAll(client, remote::jsonError(500, "Could not create " + name +
                                                   " on the SD card."));
        return;
    }
    std::setvbuf(file, nullptr, _IONBF, 0);
    // Taking the whole size at once spares the SD card from growing the
    // file, and finding its clusters, on every write.
    if (total > 0 && ftruncate(fileno(file), static_cast<off_t>(total)) != 0) {
        std::fclose(file);
        fs::removeFile(part);
        sendAll(client, remote::jsonError(507, "The SD card does not have room for " +
                                                   name + "."));
        return;
    }

    Reporter reporter(sharing, name, from, true, total);
    std::vector<char> buffer(WriteChunk);
    // Whatever of the body came in with the head goes first.
    size_t used = std::min<size_t>(data.size() - bodyStart,
                                   static_cast<size_t>(std::min<uint64_t>(
                                       total, buffer.size())));
    std::memcpy(buffer.data(), data.data() + bodyStart, used);
    uint64_t received = used;
    bool written = true;
    bool connected = true;
    while (true) {
        if (used == buffer.size() || (received == total && used > 0)) {
            written = std::fwrite(buffer.data(), 1, used, file) == used;
            used = 0;
            if (!written) {
                break;
            }
        }
        reporter.advance(received);
        if (received == total) {
            break;
        }
        const size_t want = static_cast<size_t>(
            std::min<uint64_t>(buffer.size() - used, total - received));
        const ssize_t got =
            readSome(client, buffer.data() + used, want,
                     Clock::now() + std::chrono::milliseconds(IdleTimeoutMs));
        if (got <= 0) {
            connected = false;
            break;
        }
        used += static_cast<size_t>(got);
        received += static_cast<uint64_t>(got);
    }
    written = std::fclose(file) == 0 && written;

    bool ok = written && connected;
    std::string problem = "Could not write " + name + " to the SD card.";
    if (ok && exists && !fs::removeFile(local)) {
        ok = false;
        problem = "Could not replace " + name + ".";
    }
    if (ok && !fs::renameFile(part, local)) {
        ok = false;
    }
    if (!ok) {
        fs::removeFile(part);
    }
    reporter.finish(ok);
    if (ok) {
        sendJson(client, "{\"name\":" + remote::jsonString(name) +
                             ",\"size\":" + std::to_string(total) + "}");
    } else if (connected) {
        sendAll(client, remote::jsonError(500, problem));
    }
}

// The JSON API under /api/. True when it handed something to the app.
bool serveApi(int client, const FileSharing &sharing, const std::string &from,
              remote::HttpRequest &request, std::string &data,
              size_t bodyStart, Clock::time_point deadline, Received &out)
{
    if (sharing.root.empty()) {
        sendAll(client, remote::jsonError(404, "Files are not shared."));
        return false;
    }
    std::string why;
    if (!remote::apiRequestAllowed(request, why)) {
        sendAll(client, remote::jsonError(403, why));
        return false;
    }
    const Access access = sharing.access ? sharing.access(from) : Access::Allowed;
    if (access != Access::Allowed) {
        const bool pending = access == Access::Pending;
        sendAll(client,
                remote::httpResponse(
                    403, "application/json",
                    std::string("{\"error\":") +
                        remote::jsonString(
                            pending ? "Choose Allow on the 3DS to see its files."
                                    : "The 3DS did not allow this device.") +
                        ",\"access\":\"" + (pending ? "pending" : "denied") +
                        "\"}"));
        return false;
    }

    const std::string &method = request.method;
    const std::string &route = request.path;
    std::string local;
    const auto mapped = [&](const std::string &webPath, std::string &into) {
        if (remote::localPath(sharing.root, webPath, into)) {
            return true;
        }
        sendAll(client,
                remote::jsonError(400, "That is not a path on the SD card."));
        return false;
    };

    if (route == "/api/list" && method == "GET") {
        const std::string path = remote::formValue(request.query, "path");
        if (mapped(path, local)) {
            listFolder(client, sharing, path, local);
        }
        return false;
    }
    if (route == "/api/file" && (method == "GET" || method == "PUT")) {
        if (!mapped(remote::formValue(request.query, "path"), local)) {
            return false;
        }
        if (method == "GET") {
            sendFile(client, sharing, from, local,
                     remote::formValue(request.query, "download") == "1");
        } else if (isRoot(sharing, local)) {
            sendAll(client, remote::jsonError(400, "Name the file to upload."));
        } else {
            receiveFile(client, sharing, from, request, data, bodyStart, local);
        }
        return false;
    }
    if (method != "POST") {
        sendAll(client, remote::jsonError(404, "There is nothing here."));
        return false;
    }
    if (!readBody(client, data, bodyStart, request, deadline)) {
        return false;
    }
    const std::string &form = request.body;

    if (route == "/api/folder") {
        if (!mapped(remote::formValue(form, "path"), local)) {
            return false;
        }
        if (fileops::exists(local) && !fileops::isFolder(local)) {
            sendAll(client, remote::jsonError(409, "A file is already called " +
                                                       fileops::name(local) + "."));
        } else if (!fs::makeDirectories(local)) {
            sendAll(client, remote::jsonError(500, "Could not create the folder."));
        } else {
            sendJson(client, "{}");
        }
        return false;
    }
    if (route == "/api/move") {
        std::string destination;
        if (!mapped(remote::formValue(form, "from"), local) ||
            !mapped(remote::formValue(form, "to"), destination)) {
            return false;
        }
        std::string error;
        if (isRoot(sharing, local) || isRoot(sharing, destination)) {
            sendAll(client, remote::jsonError(400, "The SD card itself cannot move."));
        } else if (!fileops::exists(local)) {
            sendAll(client, remote::jsonError(404, fileops::name(local) +
                                                       " is not there any more."));
        } else if (!fs::directoryExists(folderOf(destination))) {
            sendAll(client, remote::jsonError(404, "That folder does not exist."));
        } else if (!fileops::move(local, destination, error)) {
            sendAll(client, remote::jsonError(409, error));
        } else {
            sendJson(client, "{}");
        }
        return false;
    }
    if (route == "/api/delete") {
        if (!mapped(remote::formValue(form, "path"), local)) {
            return false;
        }
        if (isRoot(sharing, local)) {
            sendAll(client, remote::jsonError(400, "The SD card itself cannot be deleted."));
            return false;
        }
        if (!fileops::exists(local)) {
            sendJson(client, "{}"); // already gone
            return false;
        }
        NoCancel observer;
        uint64_t done = 0;
        const fileops::Result result = fileops::remove(
            local, observer, done, fileops::measure(local).files);
        if (result.status == fileops::Status::Ok) {
            sendJson(client, "{}");
        } else {
            sendAll(client, remote::jsonError(500, result.message));
        }
        return false;
    }
    if (route == "/api/install") {
        const std::string paths = remote::formValue(form, "paths");
        size_t start = 0;
        while (start < paths.size()) {
            size_t end = paths.find('\n', start);
            if (end == std::string::npos) {
                end = paths.size();
            }
            std::string path;
            if (remote::localPath(sharing.root, paths.substr(start, end - start),
                                  path) &&
                filetype::fromName(path) == filetype::Kind::Cia &&
                fs::fileExists(path) && !fileops::isFolder(path)) {
                out.install.push_back(path);
            }
            start = end + 1;
        }
        if (out.install.empty()) {
            sendAll(client, remote::jsonError(400, "There are no CIAs to install."));
            return false;
        }
        out.from = from;
        sendJson(client, "{\"count\":" + std::to_string(out.install.size()) + "}");
        return true;
    }
    sendAll(client, remote::jsonError(404, "There is nothing here."));
    return false;
}

} // namespace

void finishReceived(Received &received)
{
    if (received.replySocket >= 0) {
        const char done = 1;
        send(received.replySocket, &done, 1, sendFlags());
        closeSocket(received.replySocket);
    }
}

Receiver::~Receiver()
{
    close();
}

bool Receiver::open(unsigned short fbiPort, unsigned short webPort,
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

void Receiver::close()
{
    closeSocket(fbiSocket_);
    closeSocket(webSocket_);
    fbiPort_ = 0;
    webPort_ = 0;
}

void Receiver::shareFiles(const FileSharing &sharing)
{
    sharing_ = sharing;
    // Set on the listener, so connections start out with the larger
    // window instead of growing into it.
    if (webSocket_ >= 0 && sharing_.socketBufferSize > 0) {
        const int size = sharing_.socketBufferSize;
        setsockopt(webSocket_, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
        setsockopt(webSocket_, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    }
}

bool Receiver::wait(int timeoutMs, Received &out)
{
    out = Received();
    if (webSocket_ < 0) {
        return false;
    }
    pollfd listeners[2] = {{webSocket_, POLLIN, 0}, {fbiSocket_, POLLIN, 0}};
    const nfds_t count = fbiSocket_ >= 0 ? 2 : 1;
    if (waitSockets(listeners, count, timeoutMs, IdleStepMs) <= 0) {
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
        setNonBlocking(client);
        if (sharing_.socketBufferSize > 0) {
            const int size = sharing_.socketBufferSize;
            setsockopt(client, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
            setsockopt(client, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
        }
        const std::string from = inet_ntoa(peer.sin_addr);
        const bool got = index == 0 ? serveWeb(client, from, out)
                                    : serveFbi(client, from, out);
        if (got) {
            return true;
        }
    }
    return false;
}

bool Receiver::serveFbi(int client, const std::string &from,
                            Received &out)
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

bool Receiver::serveWeb(int client, const std::string &from, Received &out)
{
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds(ReadTimeoutMs);
    if (!firstBytes(client, webSocket_, deadline)) {
        closeSocket(client);
        return false;
    }
    std::string data;
    remote::HttpRequest request;
    size_t bodyStart = 0;
    remote::Parse state = remote::Parse::Incomplete;
    char buffer[4096];
    while (state == remote::Parse::Incomplete) {
        const ssize_t got = readSome(client, buffer, sizeof(buffer), deadline);
        if (got <= 0) {
            break;
        }
        data.append(buffer, static_cast<size_t>(got));
        state = remote::parseHead(data, request, bodyStart);
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
    if (request.path.compare(0, 5, "/api/") == 0) {
        got = serveApi(client, sharing_, from, request, data, bodyStart,
                       deadline, out);
    } else if (request.path == "/" && request.method == "GET") {
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
        if (request.method == "POST" &&
            !readBody(client, data, bodyStart, request, deadline)) {
            closeSocket(client);
            return false;
        }
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
