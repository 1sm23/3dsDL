#include "qrdec.h"

#include <3ds.h>

#include <cstdio>
#include <cstdlib>
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

void writePreview(void *framebuffer, const u16 *image)
{
    u8 *pixels = static_cast<u8 *>(framebuffer);

    for (u32 y = 0; y < CameraHeight; ++y) {
        for (u32 x = 0; x < CameraWidth; ++x) {
            const u16 value = image[y * CameraWidth + x];
            const u8 blue = ((value >> 11) & 0x1f) << 3;
            const u8 green = ((value >> 5) & 0x3f) << 2;
            const u8 red = (value & 0x1f) << 3;
            const size_t offset = (x * CameraHeight + (CameraHeight - 1 - y)) * 3;
            pixels[offset] = red;
            pixels[offset + 1] = green;
            pixels[offset + 2] = blue;
        }
    }
}

void writeIntensityMap(u8 *output, const u16 *image)
{
    for (u32 i = 0; i < CameraWidth * CameraHeight; ++i) {
        const u16 value = image[i];
        const u8 blue = ((value >> 11) & 0x1f) << 3;
        const u8 green = ((value >> 5) & 0x3f) << 2;
        const u8 red = (value & 0x1f) << 3;
        output[i] = static_cast<u8>((red + green + blue) / 3);
    }
}

Result captureFrame(u16 *buffer, u32 transferBytes)
{
    Handle receiveEvent = 0;

    Result result = CAMU_ClearBuffer(PORT_CAM1);
    if (R_FAILED(result)) {
        return result;
    }

    result = CAMU_StartCapture(PORT_CAM1);
    if (R_FAILED(result)) {
        return result;
    }

    result = CAMU_SetReceiving(&receiveEvent, reinterpret_cast<u8 *>(buffer),
                               PORT_CAM1, CameraBufferSize,
                               static_cast<s16>(transferBytes));
    if (R_SUCCEEDED(result)) {
        result = svcWaitSynchronization(receiveEvent, WaitTimeout);
    }

    CAMU_StopCapture(PORT_CAM1);
    if (receiveEvent != 0) {
        svcCloseHandle(receiveEvent);
    }
    return result;
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
    extern PrintConsole bottom;
    consoleSelect(&bottom);
    printf("\x1b[2JScan an HTTP/HTTPS QR code.\n");
    printf("Press B to cancel.\n");

    Result result = camInit();
    if (R_FAILED(result)) {
        printf("\x1b[31;1mCamera init failed: 0x%08lX\x1b[37;1m\n",
               static_cast<unsigned long>(result));
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
        printf("\x1b[31;1mCamera setup failed: 0x%08lX\x1b[37;1m\n",
               static_cast<unsigned long>(result));
        closeCamera();
        return "";
    }

    quirc *decoder = quirc_new();
    if (decoder == nullptr ||
        quirc_resize(decoder, CameraWidth, CameraHeight) < 0) {
        printf("\x1b[31;1mNot enough memory for QR scanning.\x1b[37;1m\n");
        if (decoder != nullptr) {
            quirc_destroy(decoder);
        }
        closeCamera();
        return "";
    }

    u16 *cameraBuffer = static_cast<u16 *>(malloc(CameraBufferSize));
    if (cameraBuffer == nullptr) {
        printf("\x1b[31;1mNot enough camera memory.\x1b[37;1m\n");
        quirc_destroy(decoder);
        closeCamera();
        return "";
    }

    std::string decodedUrl;

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & (KEY_B | KEY_START)) {
            break;
        }

        result = captureFrame(cameraBuffer, transferBytes);
        if (R_FAILED(result)) {
            printf("\x1b[31;1mCamera capture failed: 0x%08lX\x1b[37;1m\n",
                   static_cast<unsigned long>(result));
            break;
        }

        writePreview(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, nullptr, nullptr),
                     cameraBuffer);

        int width = CameraWidth;
        int height = CameraHeight;
        u8 *intensity = quirc_begin(decoder, &width, &height);
        writeIntensityMap(intensity, cameraBuffer);
        quirc_end(decoder);

        const int codeCount = quirc_count(decoder);
        for (int i = 0; i < codeCount; ++i) {
            quirc_code code;
            quirc_data data;
            quirc_extract(decoder, i, &code);

            const quirc_decode_error_t error = quirc_decode(&code, &data);
            if (error == QUIRC_SUCCESS) {
                decodedUrl.assign(reinterpret_cast<const char *>(data.payload),
                                  static_cast<size_t>(data.payload_len));
                break;
            }
        }

        gfxFlushBuffers();
        gspWaitForVBlank();
        gfxSwapBuffers();

        if (!decodedUrl.empty()) {
            break;
        }
    }

    free(cameraBuffer);
    quirc_destroy(decoder);
    closeCamera();

    consoleSelect(&bottom);
    if (!decodedUrl.empty()) {
        printf("\x1b[32;1mQR decoded successfully.\x1b[37;1m\n");
    }
    return decodedUrl;
}
