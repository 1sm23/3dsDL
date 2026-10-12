#include "app.hpp"
#include "ui.hpp"

#include <3ds.h>
#include <curl/curl.h>

#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <string>

u32 __stacksize__ = 0x40000;

namespace {

constexpr size_t SocBufferSize = 0x100000;

std::string failure(const char *operation, Result result)
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%s failed (0x%08lX).", operation,
                  static_cast<unsigned long>(result));
    return buffer;
}

struct Services {
    u32 *socBuffer = nullptr;
    bool romfs = false;
    bool soc = false;
    bool ac = false;
    bool curl = false;

    bool start(std::string &error)
    {
        socBuffer = static_cast<u32 *>(memalign(0x1000, SocBufferSize));
        if (socBuffer == nullptr) {
            error = "Could not allocate the network buffer.";
            return false;
        }

        Result result = romfsInit();
        if (R_FAILED(result)) {
            error = failure("Opening the app's files", result);
            return false;
        }
        romfs = true;

        result = socInit(socBuffer, SocBufferSize);
        if (R_FAILED(result)) {
            error = failure("Starting the network service", result);
            return false;
        }
        soc = true;

        // Only used for the Wi-Fi indicator, so failing here is not fatal.
        ac = R_SUCCEEDED(acInit());

        const CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (code != CURLE_OK) {
            error = std::string("Starting libcurl failed: ") +
                    curl_easy_strerror(code);
            return false;
        }
        curl = true;
        return true;
    }

    ~Services()
    {
        if (curl) {
            curl_global_cleanup();
        }
        if (ac) {
            acExit();
        }
        if (soc) {
            socExit();
        }
        if (romfs) {
            romfsExit();
        }
        std::free(socBuffer);
    }
};

} // namespace

int main()
{
    // Uses the New 3DS's faster clock and L2 cache when available. TLS and QR
    // decoding both benefit; on an Old 3DS this does nothing.
    osSetSpeedupEnable(true);
    gfxInitDefault();
    gfxSet3D(false);

    if (!ui::init()) {
        gfxExit();
        return 1;
    }

    int status = 0;
    {
        Services services;
        std::string error;
        if (services.start(error)) {
            App app;
            app.run();
        } else {
            ui::message("Startup failed", error, ui::Tone::Error);
            status = 1;
        }
    }

    ui::shutdown();
    gfxExit();
    return status;
}
