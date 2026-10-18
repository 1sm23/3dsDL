#include "download_engine.hpp"

#include "fs_util.hpp"
#include "naming.hpp"
#include "records.hpp"
#include "text.hpp"
#include "units.hpp"
#include "url.hpp"

#include <curl/curl.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>

namespace net {
namespace {

constexpr size_t FileBufferSize = 256 * 1024;
constexpr long CurlBufferSize = 128 * 1024;
constexpr long MaxRedirects = 10;

struct Transfer {
    const Request *request = nullptr;
    Observer *observer = nullptr;
    CURL *curl = nullptr;
    FILE *file = nullptr;
    std::string url;
    PartialPaths paths;
    uint64_t offset = 0; // bytes already in the file when this attempt began
    bool resuming = false;

    // Headers of the response currently being received. A redirect starts a
    // new response, which clears them.
    std::string contentDisposition;
    std::string etag;
    std::string lastModified;
    std::string acceptRanges;
    std::string contentRange;

    bool started = false; // the final response's headers have been handled
    bool cancelled = false;
    uint64_t total = 0;
    std::string filename;
    std::string failure; // set by our own checks, overrides curl's message
};

std::string errnoText()
{
    return std::strerror(errno);
}

// Runs once the headers of the final response are known, before any of its
// body is written. Returns false to abort the transfer.
bool begin(Transfer &transfer)
{
    const Request &request = *transfer.request;
    transfer.started = true;

    long status = 0;
    curl_easy_getinfo(transfer.curl, CURLINFO_RESPONSE_CODE, &status);
    char *effective = nullptr;
    curl_easy_getinfo(transfer.curl, CURLINFO_EFFECTIVE_URL, &effective);

    transfer.filename = naming::chooseFilename(
        transfer.contentDisposition,
        effective != nullptr ? effective : transfer.url);

    if (status == 206) {
        const int64_t total = contentRangeTotal(transfer.contentRange);
        transfer.total = total > 0 ? static_cast<uint64_t>(total) : 0;
    } else {
        curl_off_t length = -1;
        curl_easy_getinfo(transfer.curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,
                          &length);
        transfer.total = length >= 0
            ? transfer.offset + static_cast<uint64_t>(length)
            : 0;
    }

    transfer.observer->onFilename(transfer.filename);
    transfer.observer->onPhase(Phase::Downloading);

    if (transfer.total > request.maxFileSize) {
        transfer.failure = "The file is " + units::bytes(transfer.total) +
                           ", but the SD card's FAT32 format cannot store "
                           "files of 4 GiB or more.";
        return false;
    }
    if (request.freeSpace && transfer.total > transfer.offset) {
        const int64_t available = request.freeSpace(request.folder);
        const uint64_t needed = transfer.total - transfer.offset;
        if (available >= 0 && static_cast<uint64_t>(available) < needed) {
            transfer.failure = "Not enough free space on the SD card: " +
                               units::bytes(needed) + " needed, " +
                               units::bytes(static_cast<uint64_t>(available)) +
                               " free.";
            return false;
        }
    }

    ResumeInfo info;
    info.url = transfer.url;
    info.etag = transfer.etag;
    info.lastModified = transfer.lastModified;
    info.filename = transfer.filename;
    info.total = transfer.total;
    const bool resumable = request.allowResume &&
                           !ifRangeValidator(info).empty() &&
                           text::lower(transfer.acceptRanges) != "none";
    if (!resumable || !fs::writeFile(transfer.paths.info,
                                     serializeResumeInfo(info))) {
        fs::removeFile(transfer.paths.info);
    }
    return true;
}

size_t writeCallback(char *data, size_t size, size_t count, void *userdata)
{
    Transfer &transfer = *static_cast<Transfer *>(userdata);
    const size_t bytes = size * count;

    if (!transfer.started && !begin(transfer)) {
        return 0;
    }

    const size_t written = transfer.request->write
        ? transfer.request->write(data, bytes, transfer.file)
        : std::fwrite(data, 1, bytes, transfer.file);
    if (written != bytes && transfer.failure.empty()) {
        transfer.failure = errno == ENOSPC
            ? "The SD card is full."
            : "Could not write to the SD card (" + errnoText() + ").";
    }
    return written;
}

size_t headerCallback(char *data, size_t size, size_t count, void *userdata)
{
    Transfer &transfer = *static_cast<Transfer *>(userdata);
    const size_t bytes = size * count;
    const std::string line = text::trim(std::string(data, bytes));

    if (text::startsWithNoCase(line, "HTTP/")) {
        transfer.contentDisposition.clear();
        transfer.etag.clear();
        transfer.lastModified.clear();
        transfer.acceptRanges.clear();
        transfer.contentRange.clear();
        return bytes;
    }

    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return bytes;
    }
    const std::string name = text::lower(text::trim(line.substr(0, colon)));
    const std::string value = text::trim(line.substr(colon + 1));
    if (name == "content-disposition") {
        transfer.contentDisposition = value;
    } else if (name == "etag") {
        transfer.etag = value;
    } else if (name == "last-modified") {
        transfer.lastModified = value;
    } else if (name == "accept-ranges") {
        transfer.acceptRanges = value;
    } else if (name == "content-range") {
        transfer.contentRange = value;
    }
    return bytes;
}

int progressCallback(void *userdata, curl_off_t, curl_off_t downloaded,
                     curl_off_t, curl_off_t)
{
    Transfer &transfer = *static_cast<Transfer *>(userdata);
    if (transfer.observer->cancelRequested()) {
        transfer.cancelled = true;
        return 1;
    }
    if (transfer.started) {
        transfer.observer->onProgress(
            transfer.offset + static_cast<uint64_t>(downloaded > 0 ? downloaded
                                                                   : 0),
            transfer.total);
    } else {
        transfer.observer->onProgress(transfer.offset, 0);
    }
    return 0;
}

// Runs on each new socket before it connects, which is when the receive
// buffer has to be set for it to size the TCP window. Errors are ignored
// because the stack clamps sizes it does not like to its own limits.
int sockoptCallback(void *userdata, curl_socket_t socket, curlsocktype)
{
    const Transfer &transfer = *static_cast<Transfer *>(userdata);
    const int size = transfer.request->socketBufferSize;
    setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    return CURL_SOCKOPT_OK;
}

// Failures after which the bytes received so far are still good to resume.
bool isTransient(CURLcode code, long httpStatus)
{
    switch (code) {
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_PARTIAL_FILE:
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_SSL_CONNECT_ERROR:
        return true;
    case CURLE_HTTP_RETURNED_ERROR:
        return httpStatus >= 500;
    default:
        return false;
    }
}

void configure(Transfer &transfer, char *errorBuffer, curl_slist *headers)
{
    const Request &request = *transfer.request;
    CURL *curl = transfer.curl;

    curl_easy_setopt(curl, CURLOPT_URL, transfer.url.c_str());
    if (!request.userAgent.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, request.userAgent.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, MaxRedirects);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                     request.connectTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,
                     request.stallTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    if (!request.caFile.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, request.caFile.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, CurlBufferSize);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
    if (request.socketBufferSize > 0) {
        curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, sockoptCallback);
        curl_easy_setopt(curl, CURLOPT_SOCKOPTDATA, &transfer);
    }

    if (request.maxFileSize > transfer.offset) {
        curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE,
                         static_cast<curl_off_t>(request.maxFileSize -
                                                 transfer.offset));
    }
    if (transfer.resuming) {
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE,
                         static_cast<curl_off_t>(transfer.offset));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
}

} // namespace

Result download(const Request &request, Observer &observer)
{
    Result result;
    const std::string address = text::trim(request.url);
    if (!url::isHttpUrl(address)) {
        result.message = "Only http:// and https:// links can be downloaded.";
        return result;
    }

    Request normalised = request;
    normalised.folder = naming::normaliseFolder(request.folder);
    if (!fs::directoryExists(normalised.folder)) {
        result.message = "The folder " + normalised.folder +
                         " does not exist.";
        return result;
    }
    const PartialPaths paths = partialPaths(normalised.folder, address);

    for (int attempt = 0; attempt < 2; ++attempt) {
        Transfer transfer;
        transfer.request = &normalised;
        transfer.observer = &observer;
        transfer.url = address;
        transfer.paths = paths;

        // Continue an earlier download only when it was for the same URL and
        // the server gave a validator we can send in If-Range. Otherwise any
        // leftover data could belong to a different version of the file.
        std::string validator;
        std::string stored;
        ResumeInfo previous;
        const int64_t existing = fs::fileSize(paths.data);
        if (normalised.allowResume && attempt == 0 && existing > 0 &&
            fs::readFile(paths.info, stored) &&
            parseResumeInfo(stored, previous) && previous.url == address) {
            validator = ifRangeValidator(previous);
        }
        if (!validator.empty()) {
            transfer.offset = static_cast<uint64_t>(existing);
            transfer.resuming = true;
            transfer.filename = previous.filename;
        } else {
            fs::removeFile(paths.data);
            fs::removeFile(paths.info);
        }

        observer.onPhase(attempt == 0 ? Phase::Connecting : Phase::Restarting);

        transfer.file = std::fopen(paths.data.c_str(),
                                   transfer.resuming ? "ab" : "wb");
        if (transfer.file == nullptr) {
            result.message = "Could not create a file in " +
                             normalised.folder + " (" + errnoText() + ").";
            return result;
        }
        std::setvbuf(transfer.file, nullptr, _IOFBF, FileBufferSize);

        transfer.curl = curl_easy_init();
        if (transfer.curl == nullptr) {
            std::fclose(transfer.file);
            fs::removeFile(paths.data);
            result.message = "Could not start the HTTPS client.";
            return result;
        }

        char errorBuffer[CURL_ERROR_SIZE] = {0};
        curl_slist *headers = nullptr;
        if (transfer.resuming) {
            headers = curl_slist_append(headers,
                                        ("If-Range: " + validator).c_str());
            observer.onResumed(transfer.offset);
        }
        configure(transfer, errorBuffer, headers);

        CURLcode code = curl_easy_perform(transfer.curl);
        curl_easy_getinfo(transfer.curl, CURLINFO_RESPONSE_CODE,
                          &result.httpStatus);
        if (code == CURLE_OK && !transfer.started && !transfer.cancelled &&
            !begin(transfer)) {
            code = CURLE_WRITE_ERROR; // an empty body that failed a check
        }
        char *effective = nullptr;
        curl_easy_getinfo(transfer.curl, CURLINFO_EFFECTIVE_URL, &effective);
        result.effectiveUrl = effective != nullptr ? effective : address;

        const bool closed = std::fclose(transfer.file) == 0;
        curl_slist_free_all(headers);
        curl_easy_cleanup(transfer.curl);

        result.curlCode = code;
        result.filename = transfer.filename;
        result.resumed = transfer.resuming;

        if (transfer.cancelled) {
            const int64_t kept = fs::fileSize(paths.data);
            if (observer.keepPartialOnCancel() && normalised.allowResume &&
                closed && kept > 0 && fs::fileExists(paths.info)) {
                result.resumable = true;
                result.partPath = paths.data;
                result.bytes = static_cast<uint64_t>(kept);
            } else {
                fs::removeFile(paths.data);
                fs::removeFile(paths.info);
            }
            result.outcome = Outcome::Cancelled;
            result.message = "Download cancelled.";
            return result;
        }

        // The server no longer accepts our range: the file changed, or the
        // server ignores ranges. Start again from the beginning once.
        const bool rangeRejected =
            transfer.resuming &&
            (code == CURLE_RANGE_ERROR ||
             (code == CURLE_HTTP_RETURNED_ERROR && result.httpStatus == 416));
        if (rangeRejected && attempt == 0) {
            fs::removeFile(paths.data);
            fs::removeFile(paths.info);
            continue;
        }

        if (code == CURLE_OK && closed) {
            fs::removeFile(paths.info);
            result.outcome = Outcome::Completed;
            result.partPath = paths.data;
            const int64_t size = fs::fileSize(paths.data);
            result.bytes = size > 0 ? static_cast<uint64_t>(size) : 0;
            if (result.filename.empty()) {
                result.filename = naming::DefaultFilename;
            }
            return result;
        }

        result.outcome = Outcome::Failed;
        result.detail = errorBuffer[0] != '\0' ? errorBuffer
                                               : curl_easy_strerror(code);
        result.certificateProblem = code == CURLE_PEER_FAILED_VERIFICATION;
        if (!transfer.failure.empty()) {
            result.message = transfer.failure;
        } else if (!closed) {
            result.message = "Could not finish writing to the SD card.";
        } else {
            result.message =
                describeFailure(code, result.httpStatus, result.detail);
        }

        const int64_t kept = fs::fileSize(paths.data);
        if (normalised.allowResume && closed && transfer.failure.empty() &&
            isTransient(code, result.httpStatus) && kept > 0 &&
            fs::fileExists(paths.info)) {
            result.resumable = true;
            result.partPath = paths.data;
            result.bytes = static_cast<uint64_t>(kept);
        } else {
            fs::removeFile(paths.data);
            fs::removeFile(paths.info);
        }
        return result;
    }

    result.message = "The server kept rejecting the request to continue the "
                     "download.";
    return result;
}

uint64_t partialSize(const std::string &folder, const std::string &url)
{
    const PartialPaths paths =
        partialPaths(naming::normaliseFolder(folder), text::trim(url));
    const int64_t size = fs::fileSize(paths.data);
    return size > 0 && fs::fileExists(paths.info) ? static_cast<uint64_t>(size)
                                                  : 0;
}

void discardPartial(const std::string &folder, const std::string &url)
{
    const PartialPaths paths =
        partialPaths(naming::normaliseFolder(folder), text::trim(url));
    fs::removeFile(paths.data);
    fs::removeFile(paths.info);
}

std::string describeFailure(int curlCode, long httpStatus,
                            const std::string &detail)
{
    const auto mentions = [&](const char *needle) {
        return text::lower(detail).find(text::lower(needle)) !=
               std::string::npos;
    };

    switch (static_cast<CURLcode>(curlCode)) {
    case CURLE_OK:
        return "";
    case CURLE_UNSUPPORTED_PROTOCOL:
        return "Only http:// and https:// links are supported, and the "
               "server redirected somewhere else.";
    case CURLE_URL_MALFORMAT:
        return "That link is not a valid URL.";
    case CURLE_COULDNT_RESOLVE_HOST:
        return "Could not find the server. Check the Wi-Fi connection and "
               "the link.";
    case CURLE_COULDNT_CONNECT:
        return "Could not connect to the server. Check the Wi-Fi connection.";
    case CURLE_OPERATION_TIMEDOUT:
        return "The connection timed out because the server stopped "
               "responding.";
    case CURLE_PEER_FAILED_VERIFICATION:
        // mbedTLS (on the console) and OpenSSL (in tests) word these
        // differently, so both spellings are recognised.
        if (mentions("BADCERT_FUTURE") || mentions("not yet valid")) {
            return "The server's certificate is not valid yet according to "
                   "the console's clock, so the console's date is probably "
                   "behind. Check the clock and try again.";
        }
        if (mentions("BADCERT_EXPIRED") || mentions("expired")) {
            return "The server's certificate has expired according to the "
                   "console's clock. Check that the console's date is "
                   "right; if it is, the site's certificate really has "
                   "expired.";
        }
        if (mentions("CN_MISMATCH") || mentions("subject name") ||
            mentions("does not match")) {
            return "The server's certificate is for a different site than "
                   "the link.";
        }
        if (mentions("NOT_TRUSTED") || mentions("self-signed") ||
            mentions("self signed") || mentions("local issuer")) {
            return "The server's certificate is not signed by an authority "
                   "3dsDL trusts.";
        }
        return "The server's certificate could not be verified. If the "
               "console's date and time are wrong, fix them with Check "
               "clock and try again.";
    case CURLE_SSL_CONNECT_ERROR:
        return "The secure connection to the server failed.";
    case CURLE_SSL_CACERT_BADFILE:
        return "3dsDL could not load its certificate store.";
    case CURLE_TOO_MANY_REDIRECTS:
        return "The link redirected too many times.";
    case CURLE_FILESIZE_EXCEEDED:
        return "The file is too large for the SD card's FAT32 format "
               "(4 GiB or more).";
    case CURLE_PARTIAL_FILE:
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_GOT_NOTHING:
        return "The connection dropped before the download finished.";
    case CURLE_WRITE_ERROR:
        return "Could not write to the SD card.";
    case CURLE_RANGE_ERROR:
        return "The server refused to continue the earlier download.";
    case CURLE_HTTP_RETURNED_ERROR:
        switch (httpStatus) {
        case 401:
        case 403:
            return "The server refused access (HTTP " +
                   std::to_string(httpStatus) +
                   "). The link may need a login or may have expired.";
        case 404:
        case 410:
            return "The file was not found on the server (HTTP " +
                   std::to_string(httpStatus) + ").";
        case 429:
            return "The server is limiting requests (HTTP 429). Try again "
                   "later.";
        default:
            break;
        }
        if (httpStatus >= 500) {
            return "The server had an error (HTTP " +
                   std::to_string(httpStatus) + "). Try again later.";
        }
        return "The server answered with HTTP " + std::to_string(httpStatus) +
               ".";
    default:
        break;
    }
    return std::string("The download failed: ") +
           curl_easy_strerror(static_cast<CURLcode>(curlCode)) + ".";
}

} // namespace net
