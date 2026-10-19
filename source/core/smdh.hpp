#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The SMDH block that holds a title's name, publisher and HOME Menu icon. It
// sits in a CIA's meta section and at the end of a .3dsx.
namespace smdh {

constexpr size_t Size = 0x36c0;
constexpr uint32_t IconSize = 48;

struct Info {
    std::string shortName;
    std::string longName;
    std::string publisher;
    // 48x48 RGB565 pixels, row by row from the top left. Empty when the
    // block has no icon.
    std::vector<uint16_t> icon;
};

// Reads the English text, or the first language that has any, and decodes
// the large icon. Returns false when data is not an SMDH block.
bool parse(const uint8_t *data, size_t size, Info &info);

// UTF-16LE text that ends at the first zero or after maxUnits units.
std::string utf16ToUtf8(const uint8_t *data, size_t maxUnits);

} // namespace smdh
