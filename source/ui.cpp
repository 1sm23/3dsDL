#include "ui.hpp"

#include <citro2d.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace ui {
namespace {

constexpr u32 Background = C2D_Color32(0x08, 0x13, 0x1f, 0xff);
constexpr u32 Background2 = C2D_Color32(0x0d, 0x20, 0x31, 0xff);
constexpr u32 Surface = C2D_Color32(0x10, 0x26, 0x38, 0xff);
constexpr u32 SurfaceRaised = C2D_Color32(0x17, 0x34, 0x49, 0xff);
constexpr u32 Primary = C2D_Color32(0x34, 0xd6, 0xe8, 0xff);
constexpr u32 PrimaryDark = C2D_Color32(0x18, 0x78, 0x8a, 0xff);
constexpr u32 Text = C2D_Color32(0xf4, 0xf8, 0xfb, 0xff);
constexpr u32 Muted = C2D_Color32(0x9d, 0xb3, 0xc3, 0xff);
constexpr u32 Success = C2D_Color32(0x56, 0xe3, 0x9f, 0xff);
constexpr u32 Warning = C2D_Color32(0xff, 0xc8, 0x57, 0xff);
constexpr u32 Danger = C2D_Color32(0xff, 0x5d, 0x73, 0xff);
constexpr u32 Shadow = C2D_Color32(0x00, 0x00, 0x00, 0x55);

struct Rect {
    float x;
    float y;
    float w;
    float h;
};

C3D_RenderTarget *topTarget = nullptr;
C3D_RenderTarget *bottomTarget = nullptr;
C2D_TextBuf textBuffer = nullptr;
int homeSelection = 0;

bool contains(const Rect &rect, const touchPosition &touch)
{
    return touch.px >= rect.x && touch.px <= rect.x + rect.w &&
           touch.py >= rect.y && touch.py <= rect.y + rect.h;
}

std::string ellipsize(const std::string &value, size_t maxChars)
{
    if (value.size() <= maxChars) {
        return value;
    }
    if (maxChars <= 3) {
        return value.substr(0, maxChars);
    }
    return value.substr(0, maxChars - 3) + "...";
}

void drawText(const std::string &value, float x, float y, float size,
              u32 color, u32 flags = 0)
{
    C2D_Text text;
    C2D_TextParse(&text, textBuffer, value.c_str());
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, flags | C2D_WithColor, x, y, 0.8f,
                 size, size, color);
}

void drawBackground(float width)
{
    C2D_DrawRectangle(0, 0, 0.0f, width, 240,
                      Background, Background2, Background, Background2);
}

void drawPanel(const Rect &rect, u32 color = Surface)
{
    C2D_DrawRectSolid(rect.x + 2, rect.y + 3, 0.1f,
                      rect.w, rect.h, Shadow);
    C2D_DrawRectSolid(rect.x, rect.y, 0.2f, rect.w, rect.h, color);
    C2D_DrawRectSolid(rect.x, rect.y, 0.3f, 4, rect.h, PrimaryDark);
}

void drawHeader(const std::string &subtitle = "")
{
    drawText("3dsDL", 18, 13, 0.8f, Text);
    C2D_DrawRectSolid(18, 39, 0.4f, 42, 3, Primary);
    if (!subtitle.empty()) {
        drawText(subtitle, 382, 18, 0.65f, Muted, C2D_AlignRight);
    }
}

void drawButton(const Rect &rect, const std::string &label, bool selected,
                bool destructive = false)
{
    const u32 base = selected
        ? (destructive ? Danger : Primary)
        : SurfaceRaised;
    const u32 labelColor = selected ? Background : Text;

    C2D_DrawRectSolid(rect.x + 2, rect.y + 3, 0.3f,
                      rect.w, rect.h, Shadow);
    C2D_DrawRectSolid(rect.x, rect.y, 0.4f, rect.w, rect.h, base);

    if (selected) {
        C2D_DrawRectSolid(rect.x, rect.y, 0.5f, 5, rect.h,
                          destructive ? C2D_Color32(0xff, 0xb0, 0xbc, 0xff)
                                      : C2D_Color32(0xc5, 0xf8, 0xfc, 0xff));
    }

    drawText(label, rect.x + rect.w / 2, rect.y + rect.h / 2 - 8,
             0.62f, labelColor, C2D_AlignCenter);
}

void beginFullFrame()
{
    C2D_TextBufClear(textBuffer);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(topTarget, Background);
    C2D_SceneBegin(topTarget);
    drawBackground(400);

    C2D_TargetClear(bottomTarget, Background);
}

void beginBottomOnlyFrame()
{
    C2D_TextBufClear(textBuffer);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(bottomTarget, Background);
    C2D_SceneBegin(bottomTarget);
    drawBackground(320);
}

void drawHomeTop(const std::string &destination)
{
    C2D_SceneBegin(topTarget);
    drawHeader("ready");

    drawPanel({18, 62, 364, 72});
    drawText("DOWNLOAD LOCATION", 34, 76, 0.5f, Muted);
    drawText(ellipsize(destination, 46), 34, 101, 0.72f, Text);

    drawPanel({18, 151, 364, 60});
    drawText("Secure HTTPS downloads", 34, 165, 0.64f, Success);
    drawText("Scan a QR code or paste a direct file URL.",
             34, 188, 0.5f, Muted);
}

void drawHomeBottom(int selection)
{
    C2D_SceneBegin(bottomTarget);

    drawText("Choose an action", 18, 14, 0.58f, Muted);

    const Rect scan = {18, 45, 284, 48};
    const Rect enter = {18, 104, 284, 48};
    const Rect folder = {18, 168, 284, 42};

    drawButton(scan, "SCAN QR CODE", selection == 0);
    drawButton(enter, "ENTER URL", selection == 1);
    drawButton(folder, "CHANGE FOLDER", selection == 2);

    drawText("D-Pad + A", 18, 220, 0.42f, Muted);
    drawText("START: Exit", 302, 220, 0.42f, Muted, C2D_AlignRight);
}

void renderChoiceDialog(const std::string &title, const std::string &body,
                        const std::string &left, const std::string &right,
                        int selection, bool rightDanger)
{
    beginFullFrame();

    C2D_SceneBegin(topTarget);
    drawHeader("confirm");
    drawPanel({24, 63, 352, 125});
    drawText(title, 42, 78, 0.75f, Text);
    drawText(ellipsize(body, 53), 42, 117, 0.52f, Muted);
    drawText("Check the URL before continuing.", 42, 151, 0.48f, Warning);

    C2D_SceneBegin(bottomTarget);
    drawText("A: Select     B: Cancel", 18, 20, 0.5f, Muted);
    drawButton({18, 92, 136, 52}, left, selection == 0);
    drawButton({166, 92, 136, 52}, right, selection == 1, rightDanger);

    C3D_FrameEnd(0);
}

} // namespace

bool init()
{
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        return false;
    }
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        C3D_Fini();
        return false;
    }

    C2D_Prepare();
    topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    textBuffer = C2D_TextBufNew(8192);

    if (topTarget == nullptr || bottomTarget == nullptr || textBuffer == nullptr) {
        shutdown();
        return false;
    }
    return true;
}

void shutdown()
{
    if (textBuffer != nullptr) {
        C2D_TextBufDelete(textBuffer);
        textBuffer = nullptr;
    }
    C2D_Fini();
    C3D_Fini();
    topTarget = nullptr;
    bottomTarget = nullptr;
}

HomeAction homeFrame(const std::string &destination)
{
    hidScanInput();
    const u32 down = hidKeysDown();
    touchPosition touch{};
    hidTouchRead(&touch);

    if (down & KEY_START) {
        return HomeAction::Exit;
    }
    if (down & KEY_DUP) {
        homeSelection = (homeSelection + 2) % 3;
    }
    if (down & KEY_DDOWN) {
        homeSelection = (homeSelection + 1) % 3;
    }

    if (down & KEY_TOUCH) {
        if (contains({18, 45, 284, 48}, touch)) {
            homeSelection = 0;
            return HomeAction::ScanQr;
        }
        if (contains({18, 104, 284, 48}, touch)) {
            homeSelection = 1;
            return HomeAction::EnterUrl;
        }
        if (contains({18, 168, 284, 42}, touch)) {
            homeSelection = 2;
            return HomeAction::ChangeFolder;
        }
    }

    HomeAction action = HomeAction::None;
    if (down & KEY_A) {
        action = homeSelection == 0
            ? HomeAction::ScanQr
            : homeSelection == 1
                ? HomeAction::EnterUrl
                : HomeAction::ChangeFolder;
    }

    beginFullFrame();
    drawHomeTop(destination);
    drawHomeBottom(homeSelection);
    C3D_FrameEnd(0);
    return action;
}

bool confirmUrl(const std::string &url)
{
    int selection = 0;

    while (aptMainLoop()) {
        hidScanInput();
        const u32 down = hidKeysDown();
        touchPosition touch{};
        hidTouchRead(&touch);

        if (down & (KEY_DLEFT | KEY_DRIGHT)) {
            selection = 1 - selection;
        }
        if (down & KEY_B) {
            return false;
        }
        if (down & KEY_TOUCH) {
            if (contains({18, 92, 136, 52}, touch)) {
                return true;
            }
            if (contains({166, 92, 136, 52}, touch)) {
                return false;
            }
        }
        if (down & KEY_A) {
            return selection == 0;
        }

        renderChoiceDialog("Download this URL?", ellipsize(url, 68),
                           "DOWNLOAD", "CANCEL", selection, false);
    }
    return false;
}

void showDialog(const std::string &title, const std::string &message, bool error)
{
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & (KEY_A | KEY_B | KEY_START)) {
            return;
        }

        beginFullFrame();

        C2D_SceneBegin(topTarget);
        drawHeader(error ? "error" : "complete");
        drawPanel({24, 61, 352, 134});
        drawText(title, 42, 78, 0.76f, error ? Danger : Success);
        drawText(ellipsize(message, 72), 42, 121, 0.52f, Text);
        drawText(error ? "Press OK to return."
                       : "The file is ready on your SD card.",
                 42, 162, 0.46f, Muted);

        C2D_SceneBegin(bottomTarget);
        drawButton({58, 92, 204, 54}, "OK", true);
        drawText("Press A or B", 160, 170, 0.48f, Muted, C2D_AlignCenter);

        C3D_FrameEnd(0);
    }
}

void renderDownload(const DownloadView &view)
{
    beginFullFrame();

    C2D_SceneBegin(topTarget);
    drawHeader("downloading");

    drawPanel({18, 57, 364, 76});
    drawText(view.filename.empty() ? "Resolving filename..." :
             ellipsize(view.filename, 44), 34, 73, 0.7f, Text);
    drawText(ellipsize(view.url, 58), 34, 105, 0.45f, Muted);

    const float progress = view.total > 0
        ? std::min(1.0f, static_cast<float>(view.downloaded) /
                         static_cast<float>(view.total))
        : 0.0f;

    C2D_DrawRectSolid(24, 154, 0.3f, 352, 17, SurfaceRaised);
    if (view.total > 0) {
        C2D_DrawRectSolid(24, 154, 0.4f, 352 * progress, 17, Primary);
    } else {
        const float pulse = static_cast<float>((osGetTime() / 8) % 260);
        C2D_DrawRectSolid(24 + pulse, 154, 0.4f, 72, 17, Primary);
    }

    char amounts[96];
    const double nowMiB = static_cast<double>(view.downloaded) / 1048576.0;
    if (view.total > 0) {
        const double totalMiB = static_cast<double>(view.total) / 1048576.0;
        std::snprintf(amounts, sizeof(amounts), "%.2f / %.2f MiB",
                      nowMiB, totalMiB);
    } else {
        std::snprintf(amounts, sizeof(amounts), "%.2f MiB", nowMiB);
    }
    drawText(amounts, 24, 184, 0.55f, Text);
    drawText(view.status, 376, 184, 0.5f, Muted, C2D_AlignRight);

    C2D_SceneBegin(bottomTarget);
    drawText("Saving to", 18, 21, 0.5f, Muted);
    drawPanel({18, 48, 284, 58});
    drawText(ellipsize(view.destination, 38), 32, 67, 0.56f, Text);
    drawButton({58, 137, 204, 52}, "CANCEL DOWNLOAD", false, true);
    drawText("Press B to cancel", 160, 211, 0.45f, Muted, C2D_AlignCenter);

    C3D_FrameEnd(0);
}

ConflictChoice askFileConflict(const std::string &filename,
                               const std::string &suggestedName)
{
    int selection = 0;

    while (aptMainLoop()) {
        hidScanInput();
        const u32 down = hidKeysDown();
        touchPosition touch{};
        hidTouchRead(&touch);

        if (down & KEY_DLEFT) {
            selection = (selection + 2) % 3;
        }
        if (down & KEY_DRIGHT) {
            selection = (selection + 1) % 3;
        }
        if (down & KEY_B) {
            return ConflictChoice::Cancel;
        }

        const Rect rename = {8, 103, 98, 50};
        const Rect overwrite = {111, 103, 98, 50};
        const Rect cancel = {214, 103, 98, 50};

        if (down & KEY_TOUCH) {
            if (contains(rename, touch)) {
                return ConflictChoice::Rename;
            }
            if (contains(overwrite, touch)) {
                return ConflictChoice::Overwrite;
            }
            if (contains(cancel, touch)) {
                return ConflictChoice::Cancel;
            }
        }
        if (down & KEY_A) {
            if (selection == 0) {
                return ConflictChoice::Rename;
            }
            if (selection == 1) {
                return ConflictChoice::Overwrite;
            }
            return ConflictChoice::Cancel;
        }

        beginFullFrame();

        C2D_SceneBegin(topTarget);
        drawHeader("file conflict");
        drawPanel({22, 58, 356, 140});
        drawText("A file with this name already exists.", 40, 76, 0.6f, Warning);
        drawText(ellipsize(filename, 47), 40, 111, 0.66f, Text);
        drawText("Rename suggestion:", 40, 148, 0.46f, Muted);
        drawText(ellipsize(suggestedName, 47), 40, 169, 0.54f, Success);

        C2D_SceneBegin(bottomTarget);
        drawText("What should 3dsDL do?", 16, 30, 0.58f, Text);
        drawButton(rename, "RENAME", selection == 0);
        drawButton(overwrite, "OVERWRITE", selection == 1, true);
        drawButton(cancel, "CANCEL", selection == 2);
        drawText("Rename keeps both files.", 160, 184, 0.45f,
                 Muted, C2D_AlignCenter);

        C3D_FrameEnd(0);
    }
    return ConflictChoice::Cancel;
}

void renderScannerControls(const std::string &status)
{
    beginBottomOnlyFrame();

    drawText("QR SCANNER", 18, 17, 0.7f, Text);
    C2D_DrawRectSolid(18, 44, 0.4f, 52, 3, Primary);

    drawPanel({18, 65, 284, 72});
    drawText(status, 34, 84, 0.55f, Success);
    drawText("Hold a direct HTTP/HTTPS QR code steady.",
             34, 112, 0.42f, Muted);

    drawButton({58, 159, 204, 48}, "CANCEL", false);
    drawText("Press B", 160, 216, 0.42f, Muted, C2D_AlignCenter);

    C3D_FrameEnd(0);
}

} // namespace ui
