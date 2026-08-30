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
constexpr size_t CameraBufferSize = CameraWidth * CameraHeight * 2;
constexpr u64 WaitTimeout = 300000000ULL;

std::string resultMessage(const char *operation, Result result)
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%s failed: 0x%08lX",
                  operation, static_cast<unsigned long>(result));
    return buffer;
}

void writePreview(void *framebuffer, const u16 *image)
{
    u8 *pixels = static_cast<u8 *>(framebuffer);

    for (u32 y = 0; y < CameraHeight; ++y) {
        for (u32 x = 0; x < CameraWidth; ++x) {
            const u16 value = image[y * CameraWidth + x];
            const u8 blue = ((value >> 11) & 0x1f) << 3;
            const u8 green = ((value >> 5) & 0x3f) << 2;
            const u8 red = (value & 0x1f) << 3;
            const size_t offset =
                (x * CameraHeight + (CameraHeight - 1 - y)) * 3;

            pixels[offset] = red;
            pixels[offset + 1] = green;
            pixels[offset + 2] = blue;
        }
    }
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
        if (hidKeysDown() & (KEY_B | KEY_START)) {
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

        writePreview(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, nullptr, nullptr),
                     cameraBuffer);
        gfxFlushBuffers();

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

        ui::renderScannerControls(decodedUrl.empty()
            ? "Looking for a QR code..."
            : "QR code detected");

        if (!decodedUrl.empty()) {
            break;
        }
    }

    CAMU_StopCapture(PORT_CAM1);
    linearFree(cameraBuffer);
    quirc_destroy(decoder);
    CAMU_Activate(SELECT_NONE);
    camExit();

    if (!cameraError.empty()) {
        ui::showDialog("Camera error", cameraError, true);
    }

    return decodedUrl;
}
