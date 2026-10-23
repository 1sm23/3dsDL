#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Thin wrappers over the C library so the same code works on the SD card
// (through libctru's sdmc: device) and on a desktop file system in tests.
namespace fs {

bool fileExists(const std::string &path);
bool directoryExists(const std::string &path);
// File size in bytes, or -1 if the file cannot be inspected.
int64_t fileSize(const std::string &path);

// Creates folder and any missing parents. "sdmc:/" style device prefixes are
// left alone. Succeeds if the folder already exists.
bool makeDirectories(const std::string &folder);

bool readFile(const std::string &path, std::string &contents);
// Writes through a temporary file so a failed write never truncates path.
bool writeFile(const std::string &path, const std::string &contents);
bool removeFile(const std::string &path);
// Deletes a folder on the SD card and everything in it with one request to
// the card, which walks it far faster than one path at a time. False where
// that is not available (off the console, or outside sdmc:/) or it failed.
bool removeFolderTree(const std::string &folder);
bool renameFile(const std::string &from, const std::string &to);

// Names of the sub-folders of folder, sorted case-insensitively. Hidden
// entries (starting with '.') are left out.
std::vector<std::string> listFolders(const std::string &folder);

struct Entry {
    std::string name;
    bool folder = false;
    int64_t size = -1;    // -1 when unknown; folders only once added up
    int64_t modified = 0; // seconds since 1970, 0 when unknown
};

// The folders and then the files in folder, each sorted case-insensitively.
// Entries starting with '.' are left out unless hidden is true. ok is false
// when folder cannot be opened. Without times, modified stays 0: finding it
// costs the SD card a search of the folder for every entry.
std::vector<Entry> listEntries(const std::string &folder, bool *ok = nullptr,
                               bool hidden = false, bool times = true);
// The same, a batch at a time as the entries come off the card (in the
// card's order, not sorted), so a big folder can be shown while it is read.
// stop ends it early. Returns false when folder cannot be opened.
bool readEntries(const std::string &folder, bool hidden, bool times,
                 const std::function<void(std::vector<Entry> &&)> &take,
                 const std::atomic<bool> *stop = nullptr);

// An order-free fingerprint of a folder's own entries (names, kinds and file
// sizes, not times), to tell whether it changed between two reads. Entries
// starting with '.' are left out, so it does not depend on whether hidden
// files were read.
void addToFingerprint(uint64_t &print, const Entry &entry);
uint64_t fingerprint(const std::vector<Entry> &entries);

// Seconds since 1970 when path last changed, or 0 when unknown. On the SD
// card stat() always says 0, so this asks the card's archive instead.
int64_t modifiedTime(const std::string &path, int64_t statTime = 0);

enum class SortOrder { Name, Size, Date, Type };
// Folders always come first and are sorted by name, or newest first for
// Date, or largest first for Size once their sizes are filled in. Files
// follow in order: by name, largest first, newest first, or by extension
// and then name. reverse turns both groups around (Z to A, smallest or
// oldest first); folders still come first, and unknown sizes and dates last.
void sortEntries(std::vector<Entry> &entries, SortOrder order,
                 bool reverse = false);
// Whether a comes before b in that order.
bool entryBefore(const Entry &a, const Entry &b, SortOrder order,
                 bool reverse = false);
// Adds batch to entries already sorted in order, keeping them sorted.
void mergeEntries(std::vector<Entry> &sorted, std::vector<Entry> &&batch,
                  SortOrder order, bool reverse = false);

} // namespace fs
