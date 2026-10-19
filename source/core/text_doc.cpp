#include "text_doc.hpp"

#include "fs_util.hpp"
#include "text.hpp"

#include <cstdio>

namespace textdoc {
namespace {

constexpr const char *Bom = "\xef\xbb\xbf";

// Reads at most maxBytes from the start of path. full is set when that was
// the whole file.
bool readStart(const std::string &path, size_t maxBytes, std::string &bytes,
               bool &full)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    bytes.resize(maxBytes + 1);
    const size_t read = std::fread(&bytes[0], 1, bytes.size(), file);
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    if (failed) {
        return false;
    }
    full = read <= maxBytes;
    bytes.resize(full ? read : maxBytes);
    return true;
}

} // namespace

bool looksLikeText(const std::string &bytes)
{
    size_t control = 0;
    for (const char character : bytes) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (value == 0) {
            return false;
        }
        if (value < 0x20 && value != '\t' && value != '\n' && value != '\r' &&
            value != '\f' && value != 0x1b) {
            ++control;
        }
    }
    return control * 50 <= bytes.size();
}

Document parse(const std::string &bytes)
{
    Document document;
    size_t start = 0;
    if (bytes.compare(0, 3, Bom) == 0) {
        document.bom = true;
        start = 3;
    }
    const size_t firstBreak = bytes.find('\n', start);
    document.crlf = firstBreak != std::string::npos && firstBreak > start &&
                    bytes[firstBreak - 1] == '\r';

    while (start <= bytes.size()) {
        size_t end = bytes.find('\n', start);
        if (end == std::string::npos) {
            if (start < bytes.size()) {
                document.lines.push_back(bytes.substr(start));
                document.finalNewline = false;
            }
            break;
        }
        size_t lineEnd = end;
        if (lineEnd > start && bytes[lineEnd - 1] == '\r') {
            --lineEnd;
        }
        document.lines.push_back(bytes.substr(start, lineEnd - start));
        start = end + 1;
    }
    if (document.lines.empty()) {
        // An empty file is one empty line that can be typed into.
        document.lines.push_back("");
        document.finalNewline = false;
    }
    return document;
}

std::string serialize(const Document &document)
{
    std::string output = document.bom ? Bom : "";
    const char *newline = document.crlf ? "\r\n" : "\n";
    for (size_t index = 0; index < document.lines.size(); ++index) {
        output += document.lines[index];
        if (index + 1 < document.lines.size() || document.finalNewline) {
            output += newline;
        }
    }
    return output;
}

Load load(const std::string &path, Document &document, size_t maxBytes)
{
    std::string bytes;
    bool full = false;
    if (!readStart(path, maxBytes, bytes, full)) {
        return Load::Unreadable;
    }
    if (!looksLikeText(bytes)) {
        return Load::NotText;
    }
    document = parse(bytes);
    document.truncated = !full;
    // A cut-off last line would be saved as if it were the whole line.
    if (!full && !document.lines.empty()) {
        document.lines.pop_back();
    }
    document.readOnly = !full || !text::isValidUtf8(bytes);
    if (!text::isValidUtf8(bytes)) {
        for (std::string &line : document.lines) {
            line = text::latin1ToUtf8(line);
        }
    }
    return Load::Ok;
}

bool save(const std::string &path, const Document &document)
{
    return fs::writeFile(path, serialize(document));
}

std::vector<std::string> head(const std::string &path, size_t maxLines,
                              size_t maxBytes)
{
    std::string bytes;
    bool full = false;
    if (!readStart(path, maxBytes, bytes, full) || !looksLikeText(bytes)) {
        return {};
    }
    Document document = parse(text::sanitizeUtf8(bytes, '?'));
    if (document.lines.size() > maxLines) {
        document.lines.resize(maxLines);
    }
    return document.lines;
}

} // namespace textdoc
