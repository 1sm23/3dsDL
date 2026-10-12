#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The small text formats 3dsDL keeps on the SD card. Everything here only
// converts between structs and strings; reading and writing the files is the
// caller's job.

struct Settings {
    std::string destination = "sdmc:/";
    bool resumeDownloads = true;
    bool keepAwake = true;
    bool offerZipExtraction = false;
    // UTC offset used when setting the clock from the internet.
    bool timeZoneSet = false;
    int timeZoneMinutes = 0;
};

Settings parseSettings(const std::string &contents);
std::string serializeSettings(const Settings &settings);
// The pre-0.3 sdmc:/3dsDL.cfg file held only the folder on its first line.
std::string parseLegacyDestination(const std::string &contents);

enum class HistoryStatus { Saved, Failed, Cancelled };

struct HistoryEntry {
    int64_t time = 0;
    HistoryStatus status = HistoryStatus::Failed;
    uint64_t bytes = 0;
    bool resumable = false;
    std::string url;
    std::string folder;
    std::string path;    // where the file was saved, when it was
    std::string message; // result shown to the user
};

constexpr size_t HistoryLimit = 50;

std::vector<HistoryEntry> parseHistory(const std::string &contents);
std::string serializeHistory(const std::vector<HistoryEntry> &entries);
// Adds entry at the front and drops the oldest entries past limit.
void addHistoryEntry(std::vector<HistoryEntry> &entries, HistoryEntry entry,
                     size_t limit = HistoryLimit);

// What 3dsDL remembers about an interrupted download so it can continue it
// with an HTTP range request later.
struct ResumeInfo {
    std::string url;
    std::string etag;
    std::string lastModified;
    std::string filename;
    uint64_t total = 0; // 0 when the server did not say
};

std::string serializeResumeInfo(const ResumeInfo &info);
bool parseResumeInfo(const std::string &contents, ResumeInfo &info);
// The value for an If-Range header: a strong ETag, else Last-Modified, else
// "" which means the download cannot be resumed safely.
std::string ifRangeValidator(const ResumeInfo &info);

struct PartialPaths {
    std::string data; // the bytes received so far
    std::string info; // the ResumeInfo for them
};
// Hidden files in folder that hold an unfinished download of url.
PartialPaths partialPaths(const std::string &folder, const std::string &url);

// Total size from a Content-Range value such as "bytes 100-199/1000", or -1
// when it is missing or "*".
int64_t contentRangeTotal(const std::string &value);
