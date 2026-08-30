#include <3ds.h>
#include <curl/curl.h>

#include <cstdlib>
#include <iostream>
#include <malloc.h>
#include <string>

#include "common.h"
#include "download.hpp"
#include "fs.hpp"
#include "gfx.hpp"
#include "qrdec.h"
#include "util.hpp"

u32 __stacksize__ = 0x40000;

using namespace std;

namespace {

constexpr size_t SocBufferSize = 0x100000;

void waitForAcknowledgement()
{
    cout << "Press A to return to the menu." << endl;
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & (KEY_A | KEY_START)) {
            break;
        }
        gspWaitForVBlank();
    }
}

bool initializeNetwork(u32 *socBuffer)
{
    Result result = romfsInit();
    if (R_FAILED(result)) {
        cout << "\x1b[31;1mromfsInit failed: 0x" << hex << result
             << "\x1b[37;1m" << endl;
        return false;
    }

    result = socInit(socBuffer, SocBufferSize);
    if (R_FAILED(result)) {
        cout << "\x1b[31;1msocInit failed: 0x" << hex << result
             << "\x1b[37;1m" << endl;
        romfsExit();
        return false;
    }

    const CURLcode curlResult = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (curlResult != CURLE_OK) {
        cout << "\x1b[31;1mcurl initialization failed: "
             << curl_easy_strerror(curlResult) << "\x1b[37;1m" << endl;
        socExit();
        romfsExit();
        return false;
    }
    return true;
}

} // namespace

touchPosition touch;
PrintConsole top;
PrintConsole bottom;

bool touchInBox(touchPosition position, int x, int y, int width, int height)
{
    const int touchX = position.px;
    const int touchY = position.py;
    const u32 pressed = hidKeysDown();
    return (pressed & KEY_TOUCH) &&
           touchX > x && touchX < x + width &&
           touchY > y && touchY < y + height;
}

int main()
{
    gfxInitDefault();
    menu();
    consoleSelect(&top);

    u32 *socBuffer = static_cast<u32 *>(memalign(0x1000, SocBufferSize));
    if (socBuffer == nullptr) {
        cout << "\x1b[31;1mCould not allocate the network buffer.\x1b[37;1m" << endl;
        waitForAcknowledgement();
        gfxExit();
        return 1;
    }

    if (!initializeNetwork(socBuffer)) {
        waitForAcknowledgement();
        free(socBuffer);
        gfxExit();
        return 1;
    }

    while (aptMainLoop()) {
        hidScanInput();
        hidTouchRead(&touch);

        const u32 pressed = hidKeysDown();
        if (pressed & KEY_START) {
            break;
        }

        bool handled = false;

        if (touchInBox(touch, 5, 25, 300, 25)) {
            const string url = tl(2, "Enter URL", "Download");
            if (!url.empty()) {
                const Result result = http_download(url, ab);
                consoleSelect(&top);
                cout << (result == 0
                    ? "\x1b[33;1mDownload: \x1b[32;1mSuccess\x1b[37;1m"
                    : "\x1b[33;1mDownload: \x1b[31;1mFailed\x1b[37;1m")
                     << endl;
                waitForAcknowledgement();
            }
            handled = true;
        } else if (touchInBox(touch, 5, 60, 300, 25)) {
            consoleSelect(&top);
            cout << "Xplorer";
            xplorer();
            cout << "\x1b[2J";
            const string path(current_path);
            fs file;
            file.writefile("sdmc:/multi.cfg", path);
            cout << "Current path: " << current_path;
            handled = true;
        } else if (touchInBox(touch, 5, 100, 300, 30)) {
            consoleSelect(&bottom);
            cout << "\x1b[2J";

            const string url = qr_dec();

            cout << "\x1b[2J";
            consoleInit(GFX_TOP, &top);
            consoleSelect(&top);
            draw_border(GFX_TOP, 0, 255, 255);
            consoleSetWindow(&top, 1, 1, 47, 28);

            if (!url.empty()) {
                const Result result = http_download(url, ab);
                consoleSelect(&top);
                cout << (result == 0
                    ? "\x1b[33;1mDownload: \x1b[32;1mSuccess\x1b[37;1m"
                    : "\x1b[33;1mDownload: \x1b[31;1mFailed\x1b[37;1m")
                     << endl;
                waitForAcknowledgement();
            }
            handled = true;
        }

        if (handled) {
            ab.clear();
            menu();
        }

        gfxFlushBuffers();
        gspWaitForVBlank();
        gfxSwapBuffers();
    }

    curl_global_cleanup();
    socExit();
    romfsExit();
    free(socBuffer);
    gfxExit();
    return 0;
}
