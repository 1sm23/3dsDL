#include "mdns.hpp"

#include "text.hpp"

#include <algorithm>

namespace mdns {

namespace {

constexpr uint16_t TypeA = 1;
constexpr uint16_t TypeAny = 255;
constexpr uint16_t ClassIn = 1;
constexpr uint16_t TopBit = 0x8000; // QU in questions, cache-flush in answers
constexpr uint16_t FlagResponse = 0x8000;
constexpr uint16_t FlagAuthoritative = 0x0400;

uint16_t read16(const uint8_t *data)
{
    return static_cast<uint16_t>(data[0] << 8 | data[1]);
}

void put16(std::vector<uint8_t> &out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void put32(std::vector<uint8_t> &out, uint32_t value)
{
    put16(out, static_cast<uint16_t>(value >> 16));
    put16(out, static_cast<uint16_t>(value));
}

void putName(std::vector<uint8_t> &out, const std::string &name)
{
    size_t start = 0;
    while (start < name.size()) {
        size_t end = name.find('.', start);
        if (end == std::string::npos) {
            end = name.size();
        }
        const size_t length = std::min<size_t>(end - start, 63);
        out.push_back(static_cast<uint8_t>(length));
        out.insert(out.end(), name.begin() + start,
                   name.begin() + start + length);
        start = end + 1;
    }
    out.push_back(0);
}

// Reads the name at offset, following compression pointers, into name (in
// lower case, labels joined by '.'). offset moves past the name as it
// appears in place. False for a malformed name.
bool readName(const uint8_t *data, size_t size, size_t &offset,
              std::string &name)
{
    name.clear();
    size_t at = offset;
    bool jumped = false;
    int jumps = 0;
    while (true) {
        if (at >= size) {
            return false;
        }
        const uint8_t length = data[at];
        if (length == 0) {
            if (!jumped) {
                offset = at + 1;
            }
            return true;
        }
        if ((length & 0xC0) == 0xC0) {
            if (at + 1 >= size || ++jumps > 16) {
                return false;
            }
            if (!jumped) {
                offset = at + 2;
            }
            jumped = true;
            at = static_cast<size_t>((length & 0x3F) << 8 | data[at + 1]);
            continue;
        }
        if ((length & 0xC0) != 0 || at + 1 + length > size ||
            name.size() + length > 255) {
            return false;
        }
        if (!name.empty()) {
            name += '.';
        }
        name.append(reinterpret_cast<const char *>(data + at + 1), length);
        at += 1 + length;
    }
}

} // namespace

bool readQuery(const uint8_t *data, size_t size, const std::string &name,
               Asked &out)
{
    out = Asked();
    if (size < 12 || (read16(data + 2) & FlagResponse) != 0) {
        return false;
    }
    out.id = read16(data);
    const std::string wanted = text::lower(name);
    const uint16_t questions = read16(data + 4);
    size_t offset = 12;
    for (uint16_t index = 0; index < questions; ++index) {
        std::string asked;
        if (!readName(data, size, offset, asked) || offset + 4 > size) {
            return false;
        }
        const uint16_t type = read16(data + offset);
        const uint16_t klass = read16(data + offset + 2);
        offset += 4;
        if (text::lower(asked) == wanted &&
            (type == TypeA || type == TypeAny) &&
            (klass & ~TopBit) == ClassIn) {
            out.address = true;
            out.unicast = out.unicast || (klass & TopBit) != 0;
        }
    }
    return out.address;
}

bool claims(const uint8_t *data, size_t size, const std::string &name)
{
    if (size < 12 || (read16(data + 2) & FlagResponse) == 0) {
        return false;
    }
    const std::string wanted = text::lower(name);
    size_t offset = 12;
    for (uint16_t index = 0; index < read16(data + 4); ++index) {
        std::string skipped;
        if (!readName(data, size, offset, skipped) || offset + 4 > size) {
            return false;
        }
        offset += 4;
    }
    const unsigned records = static_cast<unsigned>(read16(data + 6)) +
                             read16(data + 8) + read16(data + 10);
    for (unsigned index = 0; index < records; ++index) {
        std::string owner;
        if (!readName(data, size, offset, owner) || offset + 10 > size) {
            return false;
        }
        if (text::lower(owner) == wanted) {
            return true;
        }
        offset += 10 + read16(data + offset + 8);
    }
    return false;
}

std::vector<uint8_t> answer(const std::string &name, uint32_t address,
                            const Asked &asked, bool legacy)
{
    std::vector<uint8_t> out;
    put16(out, legacy ? asked.id : 0);
    put16(out, FlagResponse | FlagAuthoritative);
    put16(out, legacy ? 1 : 0); // questions
    put16(out, 1);              // answers
    put16(out, 0);
    put16(out, 0);
    if (legacy) {
        putName(out, name);
        put16(out, TypeA);
        put16(out, ClassIn);
    }
    putName(out, name);
    put16(out, TypeA);
    // Cache-flush tells others this is the only address for the name; a
    // one-shot asker is an ordinary resolver that would not understand it.
    put16(out, legacy ? ClassIn : ClassIn | TopBit);
    put32(out, legacy ? 10 : 120);
    put16(out, 4);
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&address);
    out.insert(out.end(), bytes, bytes + 4);
    return out;
}

std::vector<uint8_t> probe(const std::string &name)
{
    std::vector<uint8_t> out;
    put16(out, 0);
    put16(out, 0);
    put16(out, 1);
    put16(out, 0);
    put16(out, 0);
    put16(out, 0);
    putName(out, name);
    put16(out, TypeAny);
    put16(out, ClassIn | TopBit);
    return out;
}

} // namespace mdns
