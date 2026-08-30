#include "qrdec.h"

#include "ui.hpp"

#include <3ds.h>

#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "quirc.h"
}

namespace {

constexpr u16 CameraWidth = 400;
constexpr u16 CameraHeight = 240;
constexpr u16 TextureWidth = 512;
constexpr u16 TextureHeight = 256;
constexpr size_t CameraBufferSize = CameraWidth * CameraHeight * 2;
constexpr u64 WaitTimeout = 300000000ULL;
constexpr Tex3DS_SubTexture PreviewSubTexture = {
    TextureWidth, TextureHeight, 0.0f, 1.0f, 1.0f, 0.0f
};

std::string resultMessage(const char *operation, Result result)
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%s failed: 0x%08lX",
                  operation, static_cast<unsigned long>(result));
    return buffer;
}

u32 mortonOffset(u32 x, u32 y)
{
    return (x & 1) | ((y & 1) << 1) |
           ((x & 2) << 1) | ((y & 2) << 2) |
           ((x & 4) << 2) | ((y & 4) << 3);
}

void updatePreviewTexture(C3D_Tex &texture, const u16 *image)
{
    u16 *pixels = static_cast<u16 *>(texture.data);

    // PICA200 textures store pixels in Morton-ordered 8x8 tiles.
    for (u32 y = 0; y < CameraHeight; ++y) {
        for (u32 x = 0; x < CameraWidth; ++x) {
            const u32 tile =
                ((y >> 3) * (TextureWidth >> 3) + (x >> 3)) << 6;
            pixels[tile + mortonOffset(x & 7, y & 7)] =
                image[y * CameraWidth + x];
        }
    }

    C3D_TexFlush(&texture);
}

void writeIntensityMap(u8 *output, const u16 *image)
{
    for (u32 index = 0; index < CameraWidth * CameraHeight; ++index) {
        const u16 value = image[index];
        const u8 blue = ((value >> 11) & 0x1f) << 3;
        const u8 green = ((value >> 5) & 0x3f) << 2;
        const u8 red = (value & 0x1f) << 3;
        output[index] = static_cast<u8>((red + green + blue) / 3);
    }
}

void closeCamera()
{
    CAMU_StopCapture(PORT_CAM1);
    CAMU_Activate(SELECT_NONE);
    camExit();
}

} // namespace

std::string qr_dec()
{
    Result result = camInit();
    if (R_FAILED(result)) {
        ui::showDialog("Camera unavailable",
                       resultMessage("Camera initialization", result), true);
        return "";
    }

    result = CAMU_Activate(SELECT_OUT1);
    if (R_SUCCEEDED(result)) {
        result = CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
    }
    if (R_SUCCEEDED(result)) {
        result = CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
    }
    if (R_SUCCEEDED(result)) {
        result = CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30);
    }
    if (R_SUCCEEDED(result)) {
        result = CAMU_SetNoiseFilter(SELECT_OUT1, true);
    }
    if (R_SUCCEEDED(result)) {
        result = CAMU_SetAutoExposure(SELECT_OUT1, true);
    }
    if (R_SUCCEEDED(result)) {
        result = CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
    }
    if (R_SUCCEEDED(result)) {
        result = CAMU_SetTrimming(PORT_CAM1, false);
    }

    u32 transferBytes = 0;
    if (R_SUCCEEDED(result)) {
        result = CAMU_GetMaxBytes(&transferBytes, CameraWidth, CameraHeight);
    }
    if (R_SUCCEEDED(result)) {
        result = CAMU_SetTransferBytes(PORT_CAM1, transferBytes,
                                       CameraWidth, CameraHeight);
    }

    if (R_FAILED(result)) {
        const std::string message = resultMessage("Camera setup", result);
        closeCamera();
        ui::showDialog("Camera unavailable", message, true);
        return "";
    }

    quirc *decoder = quirc_new();
    if (decoder == nullptr ||
        quirc_resize(decoder, CameraWidth, CameraHeight) < 0) {
        if (decoder != nullptr) {
            quirc_destroy(decoder);
        }
        closeCamera();
        ui::showDialog("Scanner unavailable",
                       "Not enough memory for QR decoding.", true);
        return "";
    }

    u16 *cameraBuffer =
        static_cast<u16 *>(linearAlloc(CameraBufferSize));
    if (cameraBuffer == nullptr) {
        quirc_destroy(decoder);
        closeCamera();
        ui::showDialog("Scanner unavailable",
                       "Not enough linear memory for the camera.", true);
        return "";
    }
    std::memset(cameraBuffer, 0, CameraBufferSize);

    C3D_Tex previewTexture{};
    if (!C3D_TexInit(&previewTexture, TextureWidth, TextureHeight,
                     GPU_RGB565)) {
        linearFree(cameraBuffer);
        quirc_destroy(decoder);
        closeCamera();
        ui::showDialog("Scanner unavailable",
                       "Not enough memory for the camera preview.", true);
        return "";
    }
    std::memset(previewTexture.data, 0, previewTexture.size);
    C3D_TexSetFilter(&previewTexture, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&previewTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexFlush(&previewTexture);
    const C2D_Image previewImage = {&previewTexture, &PreviewSubTexture};

    result = CAMU_ClearBuffer(PORT_CAM1);
    if (R_SUCCEEDED(result)) {
        result = CAMU_StartCapture(PORT_CAM1);
    }

    std::string decodedUrl;
    std::string cameraError = R_FAILED(result)
        ? resultMessage("Starting camera capture", result)
        : "";

    while (R_SUCCEEDED(result) && aptMainLoop()) {
        hidScanInput();
        const u32 pressed = hidKeysDown();
        touchPosition touch{};
        hidTouchRead(&touch);
        const bool touchedCancel =
            (pressed & KEY_TOUCH) &&
            touch.px >= 58 && touch.px <= 262 &&
            touch.py >= 159 && touch.py <= 207;
        if ((pressed & (KEY_B | KEY_START)) || touchedCancel) {
            break;
        }

        Handle receiveEvent = 0;
        result = CAMU_SetReceiving(&receiveEvent,
                                   reinterpret_cast<u8 *>(cameraBuffer),
                                   PORT_CAM1, CameraBufferSize,
                                   static_cast<s16>(transferBytes));

        if (R_SUCCEEDED(result)) {
            result = svcWaitSynchronization(receiveEvent, WaitTimeout);
        }
        if (receiveEvent != 0) {
            svcCloseHandle(receiveEvent);
        }
        if (R_FAILED(result)) {
            cameraError = resultMessage("Camera capture", result);
            break;
        }

        GSPGPU_InvalidateDataCache(cameraBuffer, CameraBufferSize);
        updatePreviewTexture(previewTexture, cameraBuffer);

        int width = CameraWidth;
        int height = CameraHeight;
        u8 *intensity = quirc_begin(decoder, &width, &height);
        writeIntensityMap(intensity, cameraBuffer);
        quirc_end(decoder);

        const int codeCount = quirc_count(decoder);
        for (int index = 0; index < codeCount; ++index) {
            quirc_code code;
            quirc_data data;
            quirc_extract(decoder, index, &code);

            if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
                decodedUrl.assign(
                    reinterpret_cast<const char *>(data.payload),
                    static_cast<size_t>(data.payload_len));
                break;
            }
        }

        ui::renderScannerFrame(previewImage, decodedUrl.empty()
            ? "Looking for a QR code..."
            : "QR code detected");

        if (!decodedUrl.empty()) {
            break;
        }
    }

    CAMU_StopCapture(PORT_CAM1);
    C3D_TexDelete(&previewTexture);
    linearFree(cameraBuffer);
    quirc_destroy(decoder);
    CAMU_Activate(SELECT_NONE);
    camExit();

    if (!cameraError.empty()) {
        ui::showDialog("Camera error", cameraError, true);
    }

    return decodedUrl;
}
