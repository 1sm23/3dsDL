#pragma once

#include <cstddef>
#include <string>
#include <vector>

// A text file as a list of lines, for the viewer and line editor in Files.
// Saving writes the lines back with the file's own line endings, byte order
// mark and final newline, so editing one line of a config changes only that
// line.
namespace textdoc {

// Files larger than this open read-only, showing only their start.
constexpr size_t MaxEditableBytes = 512 * 1024;

struct Document {
    std::vector<std::string> lines;
    bool crlf = false;        // "\r\n" line endings
    bool bom = false;         // starts with a UTF-8 byte order mark
    bool finalNewline = true; // the last line ends with a line break
    // Opened read-only: the file is too big, or is not valid UTF-8 and
    // saving would change bytes that were never shown.
    bool readOnly = false;
    bool truncated = false;   // only the start of the file was read
};

// True when bytes look like text: no NUL bytes and very few other control
// characters. Invalid UTF-8 still counts (Latin-1 configs exist).
bool looksLikeText(const std::string &bytes);

Document parse(const std::string &bytes);
std::string serialize(const Document &document);

enum class Load { Ok, NotText, Unreadable };
// Reads up to maxBytes; bigger files are read-only and truncated.
Load load(const std::string &path, Document &document,
          size_t maxBytes = MaxEditableBytes);
// Writes through a temporary file, so a failed save leaves the old file.
bool save(const std::string &path, const Document &document);

// The first lines of a file, for previews. Empty when it is not text.
std::vector<std::string> head(const std::string &path, size_t maxLines,
                              size_t maxBytes = 4096);

} // namespace textdoc
