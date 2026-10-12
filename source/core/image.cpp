#include "image.hpp"

namespace image {

void rgb565ToGray(const uint16_t *source, uint8_t *destination, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        const uint32_t value = source[index];
        const uint32_t red = ((value >> 11) & 0x1f) << 3;
        const uint32_t green = ((value >> 5) & 0x3f) << 2;
        const uint32_t blue = (value & 0x1f) << 3;
        // Integer approximation of Rec. 601 luma: 0.299 R + 0.587 G + 0.114 B.
        destination[index] =
            static_cast<uint8_t>((red * 77 + green * 150 + blue * 29) >> 8);
    }
}

uint32_t tiledIndex(uint32_t x, uint32_t y, uint32_t textureWidth)
{
    const uint32_t tile = ((y >> 3) * (textureWidth >> 3) + (x >> 3)) << 6;
    const uint32_t morton = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
                            ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    return tile + morton;
}

void tileImage(const uint16_t *source, uint32_t width, uint32_t height,
               uint16_t *texture, uint32_t textureWidth)
{
    for (uint32_t y = 0; y < height; ++y) {
        const uint16_t *row = source + y * width;
        for (uint32_t x = 0; x < width; ++x) {
            texture[tiledIndex(x, y, textureWidth)] = row[x];
        }
    }
}

} // namespace image
