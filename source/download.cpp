#include "download.hpp"

#include <3ds.h>
#include <curl/curl.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

using namespace std;

extern PrintConsole top;
extern PrintConsole bottom;

namespace {

struct DownloadState {
    FILE *file;
    string contentDisposition;
    curl_off_t lastDownloaded;
    u64 lastUpdate;
    bool cancelled;
};

string trim(string value)
{
    const char *whitespace = " \t\r\n";
    const size_t first = value.find_first_not_of(whitespace);
    if (first == string::npos) {
        return "";
    }

    const size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

string lower(string value)
{
    transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(tolower(c));
    });
    return value;
}

string normaliseDestination(string destination)
{
    destination = trim(destination);
    if (destination.empty() || destination == "Not set" || destination == "error") {
        destination = "sdmc:/";
    }

    if (destination.back() != '/') {
        destination += '/';
    }
    return destination;
}

string contentDispositionFilename(const string &header)
{
    if (header.empty()) {
        return "";
    }

    const string lowered = lower(header);
    size_t pos = lowered.find("filename*=");
    size_t prefixLength = 10;
    if (pos == string::npos) {
        pos = lowered.find("filename=");
        prefixLength = 9;
    }
    if (pos == string::npos) {
        return "";
    }

    string value = trim(header.substr(pos + prefixLength));
    const size_t semicolon = value.find(';');
    if (semicolon != string::npos) {
        value.erase(semicolon);
    }

    value = trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }

    const size_t encodingMarker = value.find("''");
    if (encodingMarker != string::npos) {
        value = value.substr(encodingMarker + 2);
    }
    return value;
}

string urlFilename(CURL *curl, const char *effectiveUrl)
{
    if (effectiveUrl == nullptr) {
        return "";
    }

    string path(effectiveUrl);
    const size_t query = path.find_first_of("?#");
    if (query != string::npos) {
        path.erase(query);
    }

    const size_t slash = path.find_last_of('/');
    path = slash == string::npos ? path : path.substr(slash + 1);

    int decodedLength = 0;
    char *decoded = curl_easy_unescape(curl, path.c_str(),
                                       static_cast<int>(path.size()), &decodedLength);
    if (decoded == nullptr) {
        return path;
    }

    string result(decoded, static_cast<size_t>(decodedLength));
    curl_free(decoded);
    return result;
}

string safeFilename(string filename)
{
    filename = trim(filename);

    const size_t slash = filename.find_last_of("/\\");
    if (slash != string::npos) {
        filename = filename.substr(slash + 1);
    }

    for (char &c : filename) {
        const unsigned char value = static_cast<unsigned char>(c);
        if (value < 32 || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || c == '/' || c == '\\') {
            c = '_';
        }
    }

    while (!filename.empty() &&
           (filename.back() == ' ' || filename.back() == '.')) {
        filename.pop_back();
    }

    if (filename.empty() || filename == "." || filename == "..") {
        filename = "download.bin";
    }

    // FAT supports long names, but keeping this bounded also keeps UI output sane.
    if (filename.size() > 180) {
        const size_t dot = filename.find_last_of('.');
        if (dot != string::npos && filename.size() - dot <= 16) {
            const string extension = filename.substr(dot);
            filename = filename.substr(0, 180 - extension.size()) + extension;
        } else {
            filename.resize(180);
        }
    }
    return filename;
}

bool fileExists(const string &path)
{
    FILE *file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    fclose(file);
    return true;
}

string unusedPath(const string &directory, const string &filename)
{
    string candidate = directory + filename;
    if (!fileExists(candidate)) {
        return candidate;
    }

    const size_t dot = filename.find_last_of('.');
    const string stem = (dot == string::npos || dot == 0)
        ? filename
        : filename.substr(0, dot);
    const string extension = (dot == string::npos || dot == 0)
        ? ""
        : filename.substr(dot);

    for (unsigned int i = 1; i < 10000; ++i) {
        candidate = directory + stem + " (" + to_string(i) + ")" + extension;
        if (!fileExists(candidate)) {
            return candidate;
        }
    }
    return "";
}

size_t writeCallback(char *data, size_t size, size_t count, void *userdata)
{
    DownloadState *state = static_cast<DownloadState *>(userdata);
    const size_t bytes = size * count;
    return fwrite(data, 1, bytes, state->file);
}

size_t headerCallback(char *data, size_t size, size_t count, void *userdata)
{
    DownloadState *state = static_cast<DownloadState *>(userdata);
    const size_t bytes = size * count;
    string header(data, bytes);
    if (lower(header).find("content-disposition:") == 0) {
        state->contentDisposition = trim(header.substr(header.find(':') + 1));
    }
    return bytes;
}

int progressCallback(void *userdata, curl_off_t total, curl_off_t downloaded,
                     curl_off_t, curl_off_t)
{
    DownloadState *state = static_cast<DownloadState *>(userdata);

    hidScanInput();
    if (hidKeysDown() & KEY_B) {
        state->cancelled = true;
        return 1;
    }

    const u64 now = osGetTime();
    if (downloaded == state->lastDownloaded && now - state->lastUpdate < 250) {
        return 0;
    }
    if (now - state->lastUpdate < 100 && total > 0 && downloaded < total) {
        return 0;
    }

    state->lastDownloaded = downloaded;
    state->lastUpdate = now;

    consoleSelect(&bottom);

    const double downloadedMiB = static_cast<double>(downloaded) / 1048576.0;
    if (total > 0) {
        const double totalMiB = static_cast<double>(total) / 1048576.0;
        const int percent = static_cast<int>((downloaded * 100) / total);
        printf("\x1b[13;1HDownloaded: %7.2f / %7.2f MiB  \n", downloadedMiB, totalMiB);
        printf("\x1b[14;1HProgress:   %3d%%                  \n", percent);
    } else {
        printf("\x1b[13;1HDownloaded: %7.2f MiB             \n", downloadedMiB);
        printf("\x1b[14;1HProgress: size unknown            \n");
    }
    printf("\x1b[16;1HPress B to cancel");
    gfxFlushBuffers();
    gfxSwapBuffers();
    return 0;
}

void showError(const string &message)
{
    consoleSelect(&top);
    cout << "\x1b[31;1m" << message << "\x1b[37;1m" << endl;
}

} // namespace

Result http_download(string url, string destination)
{
    url = trim(url);
    destination = normaliseDestination(destination);

    consoleSelect(&top);
    cout << "\x1b[2J";
    consoleSelect(&bottom);
    cout << "\x1b[2J";
    consoleSelect(&top);

    if (url.empty() || (lower(url).find("http://") != 0 &&
                        lower(url).find("https://") != 0)) {
        showError("QR code must contain an http:// or https:// URL.");
        return 1;
    }

    if (lower(destination).find("sdmc:/") != 0) {
        showError("Invalid download folder. Choose a folder on the SD card.");
        return 1;
    }

    cout << "\x1b[33;1mDownloading:\x1b[37;1m " << url << endl;
    cout << "\x1b[33;1mDestination:\x1b[37;1m " << destination << endl;

    const string temporaryPath = destination + ".multidownload.part";
    FILE *output = fopen(temporaryPath.c_str(), "wb");
    if (output == nullptr) {
        showError("Cannot create a file in that folder (error " +
                  to_string(errno) + ").");
        return 1;
    }

    CURL *curl = curl_easy_init();
    if (curl == nullptr) {
        fclose(output);
        remove(temporaryPath.c_str());
        showError("Could not initialize the HTTPS client.");
        return 1;
    }

    DownloadState state = {output, "", -1, 0, false};
    char errorBuffer[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "3dsDL/0.1.0 (Nintendo 3DS)");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/cacert.pem");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);

    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    char *effectiveUrl = nullptr;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effectiveUrl);

    const int closeResult = fclose(output);
    state.file = nullptr;

    if (result != CURLE_OK || closeResult != 0) {
        remove(temporaryPath.c_str());

        if (state.cancelled) {
            showError("Download cancelled; the partial file was removed.");
        } else if (closeResult != 0) {
            showError("Could not finish writing to the SD card.");
        } else {
            const string detail = errorBuffer[0] != '\0'
                ? errorBuffer
                : curl_easy_strerror(result);
            showError("Download failed: " + detail);
            if (status != 0) {
                cout << "HTTP status: " << status << endl;
            }
        }

        curl_easy_cleanup(curl);
        return 1;
    }

    string filename = contentDispositionFilename(state.contentDisposition);
    if (!filename.empty()) {
        int decodedLength = 0;
        char *decoded = curl_easy_unescape(curl, filename.c_str(),
                                           static_cast<int>(filename.size()),
                                           &decodedLength);
        if (decoded != nullptr) {
            filename.assign(decoded, static_cast<size_t>(decodedLength));
            curl_free(decoded);
        }
    }
    if (filename.empty()) {
        filename = urlFilename(curl, effectiveUrl);
    }
    filename = safeFilename(filename);

    const string finalPath = unusedPath(destination, filename);
    if (finalPath.empty() || rename(temporaryPath.c_str(), finalPath.c_str()) != 0) {
        remove(temporaryPath.c_str());
        curl_easy_cleanup(curl);
        showError("Download finished, but the file could not be named.");
        return 1;
    }

    curl_easy_cleanup(curl);

    consoleSelect(&top);
    cout << "\x1b[33;1mHTTP status:\x1b[37;1m " << status << endl;
    cout << "\x1b[33;1mSaved as:\x1b[37;1m " << finalPath << endl;
    return 0;
}
