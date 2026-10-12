#pragma once

#include <cstdint>
#include <string>

namespace finalize {

struct Result {
    bool ok = false;
    bool usedFallbackName = false;
    std::string path;    // final location of the data, even when ok is false
    std::string message; // explanation when something unusual happened
};

// Moves a finished download from partPath to folder + filename. With
// overwrite, an existing file is replaced and restored if the swap fails.
// Without it, an existing file is never touched and a "name (n).ext" variant
// is used instead.
//
// When the SD card rejects the name (for example because it is too long for
// the path), an ASCII-only name and then "download-<timestamp>.ext" are tried.
// The data is never deleted: if every name fails it stays at partPath.
Result commit(const std::string &partPath, const std::string &folder,
              const std::string &filename, bool overwrite, int64_t timestamp);

} // namespace finalize
