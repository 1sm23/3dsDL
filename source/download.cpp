#include "download.hpp"

#include "ui.hpp"

#include <3ds.h>
#include <curl/curl.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct DownloadState {
    CURL *curl = nullptr;
    FILE *file = nullptr;
    std::string url;
    std::string destination;
    std::string contentDisposition;
    std::string filename;
    std::string finalPath;
    curl_off_t downloaded = 0;
    curl_off_t total = 0;
    u64 lastUpdate = 0;
    bool targetResolved = false;
    bool overwrite = false;
    bool cancelled = false;
};

std::string trim(std::string value)
{
    const char *whitespace = " \t\r\n";
    const size_t first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return "";
    }
    const size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

std::string normaliseDestination(std::string destination)
{
    destination = trim(destination);
    if (destination.empty()) {
        destination = "sdmc:/";
    }
    if (destination.back() != '/') {
        destination += '/';
    }
    return destination;
}

bool fileExists(const std::string &path)
{
    FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    std::fclose(file);
    return true;
}

std::string basename(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string safeFilename(std::string filename)
{
    filename = trim(basename(filename));

    for (char &character : filename) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (value < 32 || character == ':' || character == '*' ||
            character == '?' || character == '"' || character == '<' ||
            character == '>' || character == '|' || character == '/' ||
            character == '\\') {
            character = '_';
        }
    }

    while (!filename.empty() &&
           (filename.back() == ' ' || filename.back() == '.')) {
        filename.pop_back();
    }

    if (filename.empty() || filename == "." || filename == "..") {
        filename = "download.bin";
    }

    if (filename.size() > 180) {
        const size_t dot = filename.find_last_of('.');
        if (dot != std::string::npos && filename.size() - dot <= 16) {
            const std::string extension = filename.substr(dot);
            filename = filename.substr(0, 180 - extension.size()) + extension;
        } else {
            filename.resize(180);
        }
    }
    return filename;
}

std::string unusedPath(const std::string &directory,
                       const std::string &filename)
{
    std::string candidate = directory + filename;
    if (!fileExists(candidate)) {
        return candidate;
    }

    const size_t dot = filename.find_last_of('.');
    const std::string stem = dot == std::string::npos || dot == 0
        ? filename
        : filename.substr(0, dot);
    const std::string extension = dot == std::string::npos || dot == 0
        ? ""
        : filename.substr(dot);

    for (unsigned int index = 1; index < 10000; ++index) {
        candidate = directory + stem + " (" + std::to_string(index) + ")" +
                    extension;
        if (!fileExists(candidate)) {
            return candidate;
        }
    }
    return "";
}

std::string contentDispositionFilename(const std::string &header)
{
    const std::string lowered = lower(header);
    size_t position = lowered.find("filename*=");
    size_t prefixLength = 10;

    if (position == std::string::npos) {
        position = lowered.find("filename=");
        prefixLength = 9;
    }
    if (position == std::string::npos) {
        return "";
    }

    std::string value = trim(header.substr(position + prefixLength));
    const size_t semicolon = value.find(';');
    if (semicolon != std::string::npos) {
        value.erase(semicolon);
    }
    value = trim(value);

    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }

    const size_t encoding = value.find("''");
    if (encoding != std::string::npos) {
        value = value.substr(encoding + 2);
    }
    return value;
}

std::string decodeUrlPart(CURL *curl, const std::string &value)
{
    int decodedLength = 0;
    char *decoded = curl_easy_unescape(curl, value.c_str(),
                                       static_cast<int>(value.size()),
                                       &decodedLength);
    if (decoded == nullptr) {
        return value;
    }

    std::string result(decoded, static_cast<size_t>(decodedLength));
    curl_free(decoded);
    return result;
}

std::string effectiveUrlFilename(CURL *curl)
{
    char *effectiveUrl = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
    if (effectiveUrl == nullptr) {
        return "";
    }

    std::string path(effectiveUrl);
    const size_t query = path.find_first_of("?#");
    if (query != std::string::npos) {
        path.erase(query);
    }
    return decodeUrlPart(curl, basename(path));
}

bool resolveTarget(DownloadState &state)
{
    std::string filename =
        contentDispositionFilename(state.contentDisposition);
    if (!filename.empty()) {
        filename = decodeUrlPart(state.curl, filename);
    }
    if (filename.empty()) {
        filename = effectiveUrlFilename(state.curl);
    }

    state.filename = safeFilename(filename);
    state.finalPath = state.destination + state.filename;

    if (!fileExists(state.finalPath)) {
        state.targetResolved = true;
        return true;
    }

    const std::string suggestedPath =
        unusedPath(state.destination, state.filename);
    const std::string suggestedName = basename(suggestedPath);

    const ui::ConflictChoice choice =
        ui::askFileConflict(state.filename, suggestedName);

    if (choice == ui::ConflictChoice::Cancel) {
        state.cancelled = true;
        return false;
    }

    if (choice == ui::ConflictChoice::Rename) {
        if (suggestedPath.empty()) {
            return false;
        }
        state.finalPath = suggestedPath;
        state.filename = suggestedName;
    } else {
        state.overwrite = true;
    }

    state.targetResolved = true;
    return true;
}

size_t writeCallback(char *data, size_t size, size_t count, void *userdata)
{
    DownloadState &state = *static_cast<DownloadState *>(userdata);
    const size_t bytes = size * count;

    if (!state.targetResolved && !resolveTarget(state)) {
        return 0;
    }

    return std::fwrite(data, 1, bytes, state.file);
}

size_t headerCallback(char *data, size_t size, size_t count, void *userdata)
{
    DownloadState &state = *static_cast<DownloadState *>(userdata);
    const size_t bytes = size * count;
    const std::string header(data, bytes);
    const std::string lowered = lower(header);

    if (lowered.find("http/") == 0) {
        state.contentDisposition.clear();
    } else if (lowered.find("content-disposition:") == 0) {
        state.contentDisposition =
            trim(header.substr(header.find(':') + 1));
    }
    return bytes;
}

int progressCallback(void *userdata, curl_off_t total, curl_off_t downloaded,
                     curl_off_t, curl_off_t)
{
    DownloadState &state = *static_cast<DownloadState *>(userdata);

    if (!aptMainLoop()) {
        state.cancelled = true;
        return 1;
    }

    hidScanInput();
    const u32 down = hidKeysDown();
    touchPosition touch{};
    hidTouchRead(&touch);

    if ((down & KEY_B) ||
        ((down & KEY_TOUCH) &&
         touch.px >= 58 && touch.px <= 262 &&
         touch.py >= 137 && touch.py <= 189)) {
        state.cancelled = true;
        return 1;
    }

    state.downloaded = downloaded;
    state.total = total;

    const u64 now = osGetTime();
    if (now - state.lastUpdate < 100 &&
        !(total > 0 && downloaded >= total)) {
        return 0;
    }
    state.lastUpdate = now;

    ui::DownloadView view;
    view.url = state.url;
    view.filename = state.filename;
    view.destination = state.destination;
    view.status = total > 0
        ? std::to_string(static_cast<int>((downloaded * 100) / total)) + "%"
        : "receiving";
    view.downloaded = downloaded;
    view.total = total;
    ui::renderDownload(view);
    return 0;
}

bool commitDownload(const std::string &temporaryPath,
                    const DownloadState &state)
{
    if (!state.overwrite) {
        if (fileExists(state.finalPath)) {
            return false;
        }
        return std::rename(temporaryPath.c_str(), state.finalPath.c_str()) == 0;
    }

    const std::string backupPath =
        unusedPath(state.destination, state.filename + ".3dsdl-backup");
    if (backupPath.empty()) {
        return false;
    }

    if (!fileExists(state.finalPath)) {
        return std::rename(temporaryPath.c_str(), state.finalPath.c_str()) == 0;
    }

    if (std::rename(state.finalPath.c_str(), backupPath.c_str()) != 0) {
        return false;
    }

    if (std::rename(temporaryPath.c_str(), state.finalPath.c_str()) != 0) {
        std::rename(backupPath.c_str(), state.finalPath.c_str());
        return false;
    }

    std::remove(backupPath.c_str());
    return true;
}

} // namespace

DownloadResult downloadFile(const std::string &inputUrl,
                            const std::string &inputDestination)
{
    DownloadResult output;
    DownloadState state;

    state.url = trim(inputUrl);
    state.destination = normaliseDestination(inputDestination);

    const std::string loweredUrl = lower(state.url);
    if (state.url.empty() ||
        (loweredUrl.find("http://") != 0 &&
         loweredUrl.find("https://") != 0)) {
        output.message = "The URL must begin with http:// or https://.";
        return output;
    }

    if (lower(state.destination).find("sdmc:/") != 0) {
        output.message = "Choose a valid folder on the SD card.";
        return output;
    }

    const std::string temporaryPath =
        state.destination + ".3dsdl-download.part";
    state.file = std::fopen(temporaryPath.c_str(), "wb");
    if (state.file == nullptr) {
        output.message = "Cannot create a file in that folder (error " +
                         std::to_string(errno) + ").";
        return output;
    }

    state.curl = curl_easy_init();
    if (state.curl == nullptr) {
        std::fclose(state.file);
        std::remove(temporaryPath.c_str());
        output.message = "Could not initialize the HTTPS client.";
        return output;
    }

    char errorBuffer[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(state.curl, CURLOPT_URL, state.url.c_str());
    curl_easy_setopt(state.curl, CURLOPT_USERAGENT,
                     "3dsDL/0.2.0 (Nintendo 3DS)");
    curl_easy_setopt(state.curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(state.curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(state.curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(state.curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(state.curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(state.curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(state.curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(state.curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(state.curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(state.curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(state.curl, CURLOPT_CAINFO, "romfs:/cacert.pem");
    curl_easy_setopt(state.curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(state.curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(state.curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(state.curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(state.curl, CURLOPT_HEADERDATA, &state);
    curl_easy_setopt(state.curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(state.curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(state.curl, CURLOPT_XFERINFODATA, &state);

    ui::DownloadView initialView;
    initialView.url = state.url;
    initialView.destination = state.destination;
    initialView.status = "connecting";
    ui::renderDownload(initialView);

    const CURLcode result = curl_easy_perform(state.curl);
    curl_easy_getinfo(state.curl, CURLINFO_RESPONSE_CODE, &output.httpStatus);

    const int closeResult = std::fclose(state.file);
    state.file = nullptr;

    if (result == CURLE_OK && !state.targetResolved) {
        if (!resolveTarget(state)) {
            state.cancelled = true;
        }
    }

    if (result != CURLE_OK || closeResult != 0 || state.cancelled) {
        std::remove(temporaryPath.c_str());
        output.cancelled = state.cancelled;

        if (state.cancelled) {
            output.message = "Download cancelled.";
        } else if (closeResult != 0) {
            output.message = "Could not finish writing to the SD card.";
        } else {
            output.message = errorBuffer[0] != '\0'
                ? errorBuffer
                : curl_easy_strerror(result);
        }

        curl_easy_cleanup(state.curl);
        return output;
    }

    if (!commitDownload(temporaryPath, state)) {
        std::remove(temporaryPath.c_str());
        output.message =
            "The download finished, but the destination file could not be updated.";
        curl_easy_cleanup(state.curl);
        return output;
    }

    output.success = true;
    output.path = state.finalPath;
    output.message = "Saved as " + state.finalPath;
    curl_easy_cleanup(state.curl);
    return output;
}
