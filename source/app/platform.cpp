#include "platform.hpp"

#include "timezone.hpp"

#include <3ds.h>

#include <cstdio>
#include <ctime>
#include <vector>

namespace platform {

Wifi wifiStatus()
{
    u32 status = 0;
    if (R_FAILED(ACU_GetWifiStatus(&status))) {
        return Wifi::Unknown;
    }
    return status == 0 ? Wifi::Disconnected : Wifi::Connected;
}

int64_t sdFreeBytes()
{
    FS_ArchiveResource resource {};
    if (R_FAILED(FSUSER_GetArchiveResource(&resource, SYSTEM_MEDIATYPE_SD))) {
        return -1;
    }
    return static_cast<int64_t>(resource.clusterSize) *
           static_cast<int64_t>(resource.freeClusters);
}

int64_t sdTotalBytes()
{
    FS_ArchiveResource resource {};
    if (R_FAILED(FSUSER_GetArchiveResource(&resource, SYSTEM_MEDIATYPE_SD))) {
        return -1;
    }
    return static_cast<int64_t>(resource.clusterSize) *
           static_cast<int64_t>(resource.totalClusters);
}

int64_t now()
{
    return static_cast<int64_t>(std::time(nullptr));
}

int systemModel()
{
    if (R_FAILED(cfguInit())) {
        return -1;
    }
    u8 model = 0;
    const Result result = CFGU_GetSystemModel(&model);
    cfguExit();
    return R_SUCCEEDED(result) ? model : -1;
}

std::string systemVersion()
{
    // Read from the NVer and CVer system titles, as System Settings does.
    OS_VersionBin nver {};
    OS_VersionBin cver {};
    char text[32] = {};
    if (R_FAILED(osGetSystemVersionDataString(&nver, &cver, text,
                                              sizeof(text)))) {
        return "";
    }
    return text;
}

bool setConsoleClock(int64_t localUnixMilliseconds, std::string &error)
{
    // System Settings keeps manual changes as an offset from the hardware
    // clock (config block 0x30001). Clearing it makes the console show
    // exactly the time written below.
    constexpr u32 UserTimeOffsetBlock = 0x30001;

    const auto explain = [&](const char *step, Result result) {
        char code[16];
        std::snprintf(code, sizeof(code), "0x%08lX",
                      static_cast<unsigned long>(result));
        error = std::string(step) + " was refused (" + code +
                "). Try the CIA build of 3dsDL, or set the clock from "
                "Rosalina: press L + Down + Select, open Miscellaneous "
                "options and choose \"Update time and date via NTP\".";
    };

    Result result = ptmSetsInit();
    if (R_FAILED(result)) {
        explain("Access to the clock service", result);
        return false;
    }
    result = cfguInit();
    if (R_FAILED(result)) {
        ptmSetsExit();
        explain("Access to the system settings", result);
        return false;
    }

    const u64 zero = 0;
    result = CFG_SetConfigInfoBlk4(sizeof(zero), UserTimeOffsetBlock, &zero);
    if (R_FAILED(result)) {
        explain("Clearing the time offset", result);
    } else {
        result = PTMSETS_SetSystemTime(localUnixMilliseconds - tz::UnixToY2kMs);
        if (R_FAILED(result)) {
            explain("Setting the time", result);
        } else {
            result = CFG_UpdateConfigSavegame();
            if (R_FAILED(result)) {
                explain("Saving the system settings", result);
            }
        }
    }

    cfguExit();
    ptmSetsExit();
    return R_SUCCEEDED(result);
}

bool keyboard(KeyboardKind kind, const std::string &hint,
              const std::string &initial, std::string &output)
{
    constexpr int MaxCharacters = 2000;

    SwkbdState state;
    swkbdInit(&state, SWKBD_TYPE_NORMAL, 2,
              kind == KeyboardKind::Name ? 120 : MaxCharacters);
    swkbdSetHintText(&state, hint.c_str());
    swkbdSetButton(&state, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&state, SWKBD_BUTTON_RIGHT,
                   kind == KeyboardKind::Url ? "Download" : "OK", true);
    if (kind == KeyboardKind::Text) {
        swkbdSetValidation(&state, SWKBD_ANYTHING, 0, 0);
        swkbdSetFeatures(&state, SWKBD_DARKEN_TOP_SCREEN | SWKBD_MULTILINE);
    } else {
        swkbdSetValidation(&state, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
        swkbdSetFeatures(&state, SWKBD_DARKEN_TOP_SCREEN);
    }
    if (!initial.empty()) {
        swkbdSetInitialText(&state, initial.c_str());
    }

    // Each UTF-16 unit becomes at most three bytes of UTF-8.
    std::vector<char> buffer(MaxCharacters * 3 + 1, '\0');
    const SwkbdButton button =
        swkbdInputText(&state, buffer.data(), buffer.size());
    if (button != SWKBD_BUTTON_RIGHT) {
        return false;
    }
    output = buffer.data();
    return true;
}

} // namespace platform
