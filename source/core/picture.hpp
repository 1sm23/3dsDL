#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Decodes PNG, JPEG, BMP, GIF (first frame) and TGA pictures and scales them
// to fit a screen, as RGB565 pixels ready for a texture.
namespace picture {

struct Picture {
    uint32_t width = 0;  // after fitting
    uint32_t height = 0;
    uint32_t sourceWidth = 0;
    uint32_t sourceHeight = 0;
    std::vector<uint16_t> pixels; // width * height, row by row
};

// Pictures above this many pixels are refused before decoding, because
// decoding needs three or four bytes per pixel and a 3DS app has little
// memory. On the console, JPEGs are decoded at a reduced size instead, so
// phone photos open too.
constexpr uint64_t MaxSourcePixels = 6 * 1000 * 1000;

// Reads the size without decoding. False when it is not a picture stb_image
// understands.
bool size(const std::string &path, uint32_t &width, uint32_t &height);

// Decodes path and fits it inside maxWidth x maxHeight, keeping its shape.
// Big pictures are shrunk with a box filter; small ones are enlarged by a
// whole factor so pixel art stays sharp. Transparent parts are drawn over
// background (RGB565).
bool load(const std::string &path, uint32_t maxWidth, uint32_t maxHeight,
          uint16_t background, Picture &picture, std::string &error);

// The scaling step on its own, for decoded 8-bit RGB (channels 3) or RGBA
// (channels 4) pixels.
void fit(const uint8_t *pixels, uint32_t channels, uint32_t width,
         uint32_t height, uint32_t maxWidth, uint32_t maxHeight,
         uint16_t background, Picture &picture);

} // namespace picture
