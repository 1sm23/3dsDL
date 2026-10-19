#include "smdh.hpp"

#include "image.hpp"
#include "text.hpp"

#include <cstring>

namespace smdh {
namespace {

constexpr size_t TitlesOffset = 0x8;
constexpr size_t TitleSize = 0x200;
constexpr size_t ShortNameUnits = 0x40;
constexpr size_t LongNameUnits = 0x80;
constexpr size_t PublisherUnits = 0x40;
constexpr size_t LargeIconOffset = 0x24c0;
constexpr int English = 1;
constexpr int Languages = 16;

uint16_t readLe16(const uint8_t *data)
{
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

void appendUtf8(std::string &output, uint32_t code)
{
    if (code < 0x80) {
        output += static_cast<char>(code);
    } else if (code < 0x800) {
        output += static_cast<char>(0xc0 | (code >> 6));
        output += static_cast<char>(0x80 | (code & 0x3f));
    } else if (code < 0x10000) {
        output += static_cast<char>(0xe0 | (code >> 12));
        output += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        output += static_cast<char>(0x80 | (code & 0x3f));
    } else {
        output += static_cast<char>(0xf0 | (code >> 18));
        output += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
        output += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        output += static_cast<char>(0x80 | (code & 0x3f));
    }
}

} // namespace

std::string utf16ToUtf8(const uint8_t *data, size_t maxUnits)
{
    std::string output;
    for (size_t index = 0; index < maxUnits; ++index) {
        uint32_t code = readLe16(data + index * 2);
        if (code == 0) {
            break;
        }
        if (code >= 0xd800 && code < 0xdc00 && index + 1 < maxUnits) {
            const uint32_t low = readLe16(data + (index + 1) * 2);
            if (low >= 0xdc00 && low < 0xe000) {
                code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                ++index;
            } else {
                code = 0xfffd;
            }
        } else if (code >= 0xd800 && code < 0xe000) {
            code = 0xfffd;
        }
        appendUtf8(output, code);
    }
    // Several titles put a line break between two parts of the name.
    return text::trim(text::singleLine(output));
}

bool parse(const uint8_t *data, size_t size, Info &info)
{
    if (size < Size || std::memcmp(data, "SMDH", 4) != 0) {
        return false;
    }

    info = Info();
    for (int pass = 0; pass <= Languages && info.shortName.empty(); ++pass) {
        const int language = pass == 0 ? English : pass - 1;
        if (pass > 0 && language == English) {
            continue;
        }
        const uint8_t *title = data + TitlesOffset + language * TitleSize;
        info.shortName = utf16ToUtf8(title, ShortNameUnits);
        info.longName = utf16ToUtf8(title + ShortNameUnits * 2, LongNameUnits);
        info.publisher = utf16ToUtf8(
            title + (ShortNameUnits + LongNameUnits) * 2, PublisherUnits);
    }

    // The icon is stored the way the GPU reads textures: 8x8 tiles, each in
    // Morton order.
    const uint8_t *icon = data + LargeIconOffset;
    bool blank = true;
    info.icon.resize(IconSize * IconSize);
    for (uint32_t y = 0; y < IconSize; ++y) {
        for (uint32_t x = 0; x < IconSize; ++x) {
            const uint16_t pixel =
                readLe16(icon + image::tiledIndex(x, y, IconSize) * 2);
            info.icon[y * IconSize + x] = pixel;
            blank = blank && pixel == 0;
        }
    }
    if (blank) {
        info.icon.clear();
    }
    return true;
}

} // namespace smdh
