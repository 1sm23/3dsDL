#pragma once

#include <cstddef>
#include <functional>
#include <string>

// Turning server-supplied names into safe SD card paths.
namespace naming {

constexpr size_t MaxFilenameBytes = 180;
constexpr const char *DefaultFilename = "download.bin";

// Makes name safe to use as one path component on FAT: strips any directory
// part, replaces reserved and control characters and invalid UTF-8, and
// shortens long names on a character boundary while keeping the extension.
std::string safeFilename(const std::string &name);

// Same as safeFilename but replaces every non-ASCII character as well. Used as
// a fallback when the SD card rejects a name.
std::string asciiFilename(const std::string &name);

// The filename parameter of a Content-Disposition header value (RFC 6266).
// filename* wins over filename; UTF-8 and ISO-8859-1 are understood. The
// result is not sanitised yet. Returns "" when the header carries no name.
std::string contentDispositionFilename(const std::string &header);

// Percent-decoded last path segment of a URL, not sanitised yet.
std::string urlFilename(const std::string &url);

// The sanitised name to save a download as.
std::string chooseFilename(const std::string &contentDisposition,
                           const std::string &effectiveUrl);

std::string stem(const std::string &filename);
// Extension including the dot, or "" when there is none.
std::string extension(const std::string &filename);

using ExistsFunction = std::function<bool(const std::string &)>;

// folder + filename if that path is free, otherwise the first free
// "name (n).ext" variant. Returns "" if no free name is found.
std::string unusedPath(const std::string &folder, const std::string &filename,
                       const ExistsFunction &exists, unsigned limit = 9999);

// Trims folder, defaults it to sdmc:/ and makes sure it ends with '/'.
std::string normaliseFolder(const std::string &folder);
bool isSdPath(const std::string &path);
// "sdmc:/a/b/" -> "sdmc:/a/". The root is its own parent.
std::string parentFolder(const std::string &folder);

} // namespace naming
