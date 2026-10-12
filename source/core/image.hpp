#pragma once

#include <cstddef>
#include <cstdint>

namespace image {

// Converts RGB565 pixels (red in the top bits, as the 3DS camera and GPU use
// them) to 8-bit luminance for the QR decoder.
void rgb565ToGray(const uint16_t *source, uint8_t *destination, size_t count);

// Index of pixel (x, y) inside a PICA200 texture of the given width. The GPU
// stores textures as 8x8 tiles whose pixels follow a Morton (Z-order) curve.
uint32_t tiledIndex(uint32_t x, uint32_t y, uint32_t textureWidth);

// Copies a width x height image into the top-left corner of a tiled texture.
void tileImage(const uint16_t *source, uint32_t width, uint32_t height,
               uint16_t *texture, uint32_t textureWidth);

} // namespace image
