#include <3ds.h>
#include <curl/curl.h>

#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <malloc.h>
#include <string>

#include "download.hpp"
#include "qrdec.h"
#include "ui.hpp"

u32 __stacksize__ = 0x40000;

namespace {

constexpr size_t SocBufferSize = 0x100000;
constexpr const char *ConfigPath = "sdmc:/3dsDL.cfg";

std::string trim(std::string value)
{
    const char *whitespace = " \t\r\n";
    const size_t first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return "";
    }
    return value.substr(first, value.find_last_not_of(whitespace) - first + 1);
}

std::string normaliseFolder(std::string folder)
{
    folder = trim(folder);
    if (folder.empty()) {
        return "sdmc:/";
    }
    if (folder.back() != '/') {
        folder += '/';
    }
    return folder;
}

bool folderExists(const std::string &folder)
{
    DIR *directory = opendir(folder.c_str());
    if (directory == nullptr) {
        return false;
    }
    closedir(directory);
    return true;
}

std::string loadDestination()
{
    std::ifstream file(ConfigPath);
    std::string destination;
    if (file.is_open()) {
        std::getline(file, destination);
    }

    destination = normaliseFolder(destination);
    return folderExists(destination) ? destination : "sdmc:/";
}

bool saveDestination(const std::string &destination)
{
    std::ofstream file(ConfigPath, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file << destination;
    return file.good();
}

std::string keyboardInput(const std::string &hint,
                          const std::string &initialText = "")
{
    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, -1);
    swkbdSetHintText(&keyboard, hint.c_str());
    swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "Continue", true);
    swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    if (!initialText.empty()) {
        swkbdSetInitialText(&keyboard, initialText.c_str());
    }

    char buffer[2048] = {0};
    const SwkbdButton button =
        swkbdInputText(&keyboard, buffer, sizeof(buffer));

    return button == SWKBD_BUTTON_RIGHT ? std::string(buffer) : "";
}

void runDownload(const std::string &url, const std::string &destination)
{
    if (url.empty() || !ui::confirmUrl(url)) {
        return;
    }

    const DownloadResult result = downloadFile(url, destination);
    if (result.success) {
        ui::showDialog("Download complete", result.message, false);
    } else if (!result.cancelled) {
        ui::showDialog("Download failed", result.message, true);
    }
}

bool initializeServices(u32 *socBuffer, std::string &error)
{
    Result result = romfsInit();
    if (R_FAILED(result)) {
        char buffer[80];
        std::snprintf(buffer, sizeof(buffer), "romfsInit failed: 0x%08lX",
                      static_cast<unsigned long>(result));
        error = buffer;
        return false;
    }

    result = socInit(socBuffer, SocBufferSize);
    if (R_FAILED(result)) {
        char buffer[80];
        std::snprintf(buffer, sizeof(buffer), "socInit failed: 0x%08lX",
                      static_cast<unsigned long>(result));
        error = buffer;
        romfsExit();
        return false;
    }

    const CURLcode curlResult = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (curlResult != CURLE_OK) {
        error = std::string("curl initialization failed: ") +
                curl_easy_strerror(curlResult);
        socExit();
        romfsExit();
        return false;
    }
    return true;
}

} // namespace

int main()
{
    gfxInitDefault();
    gfxSet3D(false);

    if (!ui::init()) {
        gfxExit();
        return 1;
    }

    u32 *socBuffer = static_cast<u32 *>(memalign(0x1000, SocBufferSize));
    if (socBuffer == nullptr) {
        ui::showDialog("Startup failed",
                       "Could not allocate the network buffer.", true);
        ui::shutdown();
        gfxExit();
        return 1;
    }

    std::string startupError;
    if (!initializeServices(socBuffer, startupError)) {
        ui::showDialog("Startup failed", startupError, true);
        std::free(socBuffer);
        ui::shutdown();
        gfxExit();
        return 1;
    }

    std::string destination = loadDestination();
    bool running = true;

    while (running && aptMainLoop()) {
        const ui::HomeAction action = ui::homeFrame(destination);

        switch (action) {
        case ui::HomeAction::ScanQr: {
            const std::string url = qr_dec();
            runDownload(url, destination);
            break;
        }
        case ui::HomeAction::EnterUrl: {
            const std::string url =
                keyboardInput("Enter a direct HTTP/HTTPS file URL");
            runDownload(url, destination);
            break;
        }
        case ui::HomeAction::ChangeFolder: {
            std::string selected =
                keyboardInput("SD folder, for example sdmc:/downloads/",
                              destination);
            if (selected.empty()) {
                break;
            }

            selected = normaliseFolder(selected);
            if (selected.find("sdmc:/") != 0 || !folderExists(selected)) {
                ui::showDialog("Folder not found",
                               "Enter an existing folder beginning with sdmc:/.",
                               true);
                break;
            }

            destination = selected;
            if (!saveDestination(destination)) {
                ui::showDialog("Settings warning",
                               "The folder works, but it could not be saved.",
                               true);
            }
            break;
        }
        case ui::HomeAction::Exit:
            running = false;
            break;
        case ui::HomeAction::None:
            break;
        }
    }

    curl_global_cleanup();
    socExit();
    romfsExit();
    std::free(socBuffer);
    ui::shutdown();
    gfxExit();
    return 0;
}
