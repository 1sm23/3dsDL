#pragma once

#include "archive_writer.hpp"

#include <cstdint>
#include <string>
#include <vector>

// 3dsDL's own ZIP reader, the fast path for ZIP archives. It knows every
// entry from the central directory, extracts stored and deflated entries
// in the order they are stored through one read buffer, handles ZIP64
// sizes and UTF-8 names, refuses paths that would escape the target folder,
// and checks every CRC.
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

bool encrypted(const Entry &entry);
// Stored or deflated and not encrypted: what this reader extracts.
bool readable(const Entry &entry);

archives::Result extract(const std::string &zipPath, const Archive &archive,
                        const std::string &folder,
                        const archives::Options &options,
                        archives::Observer &observer);

} // namespace zip
