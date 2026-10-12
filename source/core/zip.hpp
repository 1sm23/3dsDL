#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A small ZIP reader for the opt-in "extract after download" feature. It
// handles stored and deflated entries, ZIP64 sizes and UTF-8 names, refuses
// paths that would escape the target folder, and checks every CRC.
namespace zip {

struct Entry {
    std::string name; // as stored in the archive (made valid UTF-8)
    std::string path; // safe relative path, "" when the entry must be skipped
    uint64_t compressedSize = 0;
    uint64_t size = 0;
    uint64_t localHeaderOffset = 0;
    uint32_t crc = 0;
    uint16_t method = 0;
    uint16_t flags = 0;
    bool directory = false;
};

struct Archive {
    std::vector<Entry> entries;
    uint64_t totalSize = 0; // uncompressed bytes of all files
    unsigned fileCount = 0;
};

bool readArchive(const std::string &zipPath, Archive &archive,
                 std::string &error);

// "a\\b/../c" style names become "" (rejected); normal names come back with
// '/' separators and each component made safe for FAT.
std::string safeRelativePath(const std::string &name);

// Relative paths of files in archive that already exist in folder.
std::vector<std::string> existingFiles(const Archive &archive,
                                       const std::string &folder);

class Observer {
public:
    virtual ~Observer() = default;
    virtual bool cancelRequested() = 0;
    virtual void onProgress(uint64_t done, uint64_t total,
                            const std::string &current) = 0;
};

enum class Existing { Overwrite, Skip };

struct Result {
    bool ok = false;
    bool cancelled = false;
    unsigned extracted = 0;
    unsigned skippedExisting = 0;
    unsigned skippedUnsafe = 0;      // paths that tried to leave the folder
    unsigned skippedUnsupported = 0; // encrypted or unknown compression
    std::string message;
};

Result extract(const std::string &zipPath, const Archive &archive,
               const std::string &folder, Existing existing,
               Observer &observer);

} // namespace zip
