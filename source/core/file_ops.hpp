#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// Copying, moving, deleting and measuring files and whole folders for Files.
// Paths name the entry itself, without a trailing '/', even for folders:
// "sdmc:/3ds/app", not "sdmc:/3ds/app/".
namespace fileops {

class Observer {
public:
    virtual ~Observer() = default;
    virtual bool cancelRequested() = 0;
    // done and total count bytes across the whole operation; current is the
    // file being worked on.
    virtual void onProgress(uint64_t done, uint64_t total,
                            const std::string &current)
    {
        (void)done;
        (void)total;
        (void)current;
    }
};

struct Totals {
    uint64_t files = 0;
    uint64_t folders = 0; // below path, not counting path itself
    uint64_t bytes = 0;
};

// Everything at path, which can be a file or a folder. Setting stop ends
// the count early, with what was counted so far.
Totals measure(const std::string &path,
               const std::atomic<bool> *stop = nullptr);

enum class Status { Ok, Cancelled, Failed };

struct Result {
    Status status = Status::Ok;
    std::string message; // set when status is not Ok
    uint64_t files = 0;  // files copied or deleted
};

// Copies the file or folder at source to destination, which must not exist.
// done is advanced by the bytes copied, so several copies can share one
// progress bar of total bytes. A cancelled or failed file copy is removed;
// files already copied stay.
Result copy(const std::string &source, const std::string &destination,
            Observer &observer, uint64_t &done, uint64_t total);

// Deletes a file, or a folder with everything in it. done counts deleted
// files against total, like copy does with bytes.
Result remove(const std::string &path, Observer &observer, uint64_t &done,
              uint64_t total);

// Renames or moves within the SD card. Fails when destination exists.
bool move(const std::string &source, const std::string &destination,
          std::string &error);

bool isFolder(const std::string &path);
bool exists(const std::string &path);
// True when path is folder itself or anything below it.
bool isInside(const std::string &path, const std::string &folder);
// Last part of a path: "sdmc:/a/b" -> "b".
std::string name(const std::string &path);

// Checks a name typed for a new or renamed file or folder. FAT does not
// allow \ / : * ? " < > | and names cannot be "." or "..".
bool validName(const std::string &name, std::string &error);

// Seconds since 1970 when path was last changed, or 0 when unknown.
int64_t modifiedTime(const std::string &path);

} // namespace fileops
