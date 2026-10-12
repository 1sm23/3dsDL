#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

// The HTTP(S) download engine. It has no user interface and no libctru
// dependency, so the 3DS app runs it on a worker thread and the host tests
// run it against a local fixture server.
namespace net {

enum class Phase { Connecting, Downloading, Restarting };

// Receives updates from the transfer. Every method is called on the thread
// that runs download().
class Observer {
public:
    virtual ~Observer() = default;
    virtual bool cancelRequested() = 0;
    virtual void onPhase(Phase) {}
    virtual void onFilename(const std::string &) {}
    // received counts bytes already on the SD card, including resumed data.
    // total is 0 while the size is unknown.
    virtual void onProgress(uint64_t received, uint64_t total)
    {
        (void)received;
        (void)total;
    }
    virtual void onResumed(uint64_t offset) { (void)offset; }
    // When a cancelled transfer could be resumed, keep its partial file
    // instead of deleting it. Used when the app is closed mid-download.
    virtual bool keepPartialOnCancel() { return false; }
};

struct Request {
    std::string url;
    std::string folder; // existing folder, ending with '/'
    std::string caFile; // PEM bundle; "" uses libcurl's default
    std::string userAgent;
    bool allowResume = true;
    long connectTimeoutSeconds = 30;
    // Give up when less than one byte per second arrives for this long.
    long stallTimeoutSeconds = 60;
    // FAT32 cannot hold files of 4 GiB or more.
    uint64_t maxFileSize = 0xffffffffULL;
    // Free bytes in a folder, or -1 when unknown. Optional.
    std::function<int64_t(const std::string &)> freeSpace;
    // Test seam for simulating SD card failures. Defaults to fwrite.
    std::function<size_t(const void *, size_t, FILE *)> write;
};

enum class Outcome { Completed, Failed, Cancelled };

struct Result {
    Outcome outcome = Outcome::Failed;
    std::string partPath;     // finished data, or the kept partial download
    std::string filename;     // sanitised name from the server or the URL
    std::string effectiveUrl; // after redirects
    uint64_t bytes = 0;       // size of the data in partPath
    long httpStatus = 0;
    int curlCode = 0;
    bool resumable = false; // a failed download was kept for a later retry
    bool resumed = false;   // this run continued an earlier partial download
    // The server's certificate was rejected; often a wrong console clock.
    bool certificateProblem = false;
    std::string message;    // for the user
    std::string detail;     // technical detail, may be empty
};

Result download(const Request &request, Observer &observer);

// Size of a kept partial download of url in folder, or 0 when there is none.
uint64_t partialSize(const std::string &folder, const std::string &url);
void discardPartial(const std::string &folder, const std::string &url);

// A message for the user explaining a libcurl failure. detail is libcurl's
// error text, which says why a certificate was rejected.
std::string describeFailure(int curlCode, long httpStatus,
                            const std::string &detail = "");

} // namespace net
