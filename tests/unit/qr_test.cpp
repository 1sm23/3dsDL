#include "../check.hpp"

#include "image.hpp"

extern "C" {
#include "../third_party/qrcodegen.h"
#include "quirc.h"
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

// These tests draw QR codes into a 400x240 RGB565 frame, like the one the 3DS
// camera delivers, and run it through the same conversion and the vendored
// quirc that the scanner uses.

namespace {

constexpr int Width = 400;
constexpr int Height = 240;

uint16_t rgb565(int red, int green, int blue)
{
    return static_cast<uint16_t>(((red >> 3) << 11) | ((green >> 2) << 5) |
                                 (blue >> 3));
}

struct Scene {
    double moduleSize = 4.0;
    double angle = 0.0;     // radians
    double shading = 0.0;   // 0..1 brightness falloff across the frame
    int noise = 0;          // +/- amplitude per channel
    bool tinted = false;    // draw dark modules in blue instead of black
};

std::vector<uint16_t> render(const std::string &payload, const Scene &scene)
{
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
    std::vector<uint16_t> frame(Width * Height);
    if (!qrcodegen_encodeText(payload.c_str(), temp, qr, qrcodegen_Ecc_MEDIUM,
                              qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                              qrcodegen_Mask_AUTO, true)) {
        return frame;
    }
    const int size = qrcodegen_getSize(qr);
    const double half = size * scene.moduleSize / 2.0;
    const double cosA = std::cos(scene.angle);
    const double sinA = std::sin(scene.angle);
    uint32_t seed = 12345;

    for (int y = 0; y < Height; ++y) {
        for (int x = 0; x < Width; ++x) {
            const double dx = x - Width / 2.0;
            const double dy = y - Height / 2.0;
            const double u = (cosA * dx + sinA * dy + half) / scene.moduleSize;
            const double v = (-sinA * dx + cosA * dy + half) / scene.moduleSize;
            const int mx = static_cast<int>(std::floor(u));
            const int my = static_cast<int>(std::floor(v));
            const bool dark = mx >= 0 && my >= 0 && mx < size && my < size &&
                              qrcodegen_getModule(qr, mx, my);

            const double light = 1.0 - scene.shading * x / Width;
            int red = dark ? (scene.tinted ? 20 : 15) : 235;
            int green = dark ? (scene.tinted ? 30 : 15) : 235;
            int blue = dark ? (scene.tinted ? 140 : 15) : 225;
            red = static_cast<int>(red * light);
            green = static_cast<int>(green * light);
            blue = static_cast<int>(blue * light);
            if (scene.noise > 0) {
                seed = seed * 1103515245u + 12345u;
                const int jitter = static_cast<int>((seed >> 16) %
                                                    (2 * scene.noise + 1)) -
                                   scene.noise;
                red = std::min(255, std::max(0, red + jitter));
                green = std::min(255, std::max(0, green + jitter));
                blue = std::min(255, std::max(0, blue + jitter));
            }
            frame[y * Width + x] = rgb565(red, green, blue);
        }
    }
    return frame;
}

std::string decode(const std::vector<uint16_t> &frame)
{
    quirc *decoder = quirc_new();
    if (decoder == nullptr || quirc_resize(decoder, Width, Height) < 0) {
        quirc_destroy(decoder);
        return "<no memory>";
    }
    int width = 0;
    int height = 0;
    uint8_t *gray = quirc_begin(decoder, &width, &height);
    image::rgb565ToGray(frame.data(), gray, frame.size());
    quirc_end(decoder);

    std::string result;
    for (int index = 0; index < quirc_count(decoder); ++index) {
        quirc_code code;
        quirc_data data;
        quirc_extract(decoder, index, &code);
        if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
            result.assign(reinterpret_cast<const char *>(data.payload),
                          static_cast<size_t>(data.payload_len));
            break;
        }
    }
    quirc_destroy(decoder);
    return result;
}

} // namespace

TEST("image: RGB565 to gray uses the right channels")
{
    const uint16_t pixels[] = {0x0000, 0xffff, rgb565(255, 0, 0),
                               rgb565(0, 255, 0), rgb565(0, 0, 255)};
    uint8_t gray[5] = {};
    image::rgb565ToGray(pixels, gray, 5);
    CHECK_EQ(static_cast<int>(gray[0]), 0);
    CHECK(gray[1] >= 250);
    // Green carries the most luminance and blue the least.
    CHECK(gray[3] > gray[2]);
    CHECK(gray[2] > gray[4]);
}

TEST("image: tiled texture layout")
{
    CHECK_EQ(image::tiledIndex(0, 0, 512), 0u);
    CHECK_EQ(image::tiledIndex(1, 0, 512), 1u);
    CHECK_EQ(image::tiledIndex(0, 1, 512), 2u);
    CHECK_EQ(image::tiledIndex(7, 7, 512), 63u);
    CHECK_EQ(image::tiledIndex(8, 0, 512), 64u);
    CHECK_EQ(image::tiledIndex(0, 8, 512), 64u * 64u);

    // Every pixel of a 400x240 frame must land in its own texel.
    std::set<uint32_t> seen;
    for (uint32_t y = 0; y < 240; ++y) {
        for (uint32_t x = 0; x < 400; ++x) {
            seen.insert(image::tiledIndex(x, y, 512));
        }
    }
    CHECK_EQ(seen.size(), static_cast<size_t>(400 * 240));
    CHECK(*seen.rbegin() < 512u * 256u);
}

TEST("qr: decodes a clean code")
{
    const std::string payload = "https://example.com/homebrew.3dsx";
    CHECK_EQ(decode(render(payload, Scene())), payload);
}

TEST("qr: decodes upper-case alphanumeric links")
{
    const std::string payload = "HTTPS://EXAMPLE.COM/FILE.CIA";
    CHECK_EQ(decode(render(payload, Scene())), payload);
}

TEST("qr: decodes a rotated, shaded, noisy and tinted code")
{
    Scene scene;
    scene.moduleSize = 3.0;
    scene.angle = 0.3;
    scene.shading = 0.45;
    scene.noise = 18;
    scene.tinted = true;
    const std::string payload =
        "https://github.com/ankushKun/3dsDL/releases/download/v0.3.0/"
        "3dsDL.cia";
    CHECK_EQ(decode(render(payload, scene)), payload);
}

TEST("qr: hostile stripe patterns do not crash the decoder")
{
    // Dense stripes once produced timing-pattern counts far above version 40,
    // which overran quirc_extract's bitmap in the old vendored copy. Running
    // this under AddressSanitizer guards against that coming back.
    std::vector<uint16_t> frame(Width * Height);
    for (int period = 2; period <= 6; ++period) {
        for (int y = 0; y < Height; ++y) {
            for (int x = 0; x < Width; ++x) {
                const bool dark = ((x / period) + (y / 37)) % 2 == 0 ||
                                  ((x / 41 + y / 41) % 5 == 0);
                frame[y * Width + x] = dark ? 0x0000 : 0xffff;
            }
        }
        decode(frame);
    }
    CHECK(true);
}
