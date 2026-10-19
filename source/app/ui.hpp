#pragma once

#include <3ds.h>
#include <citro2d.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The Citro2D toolkit every 3dsDL screen is drawn with: colours, text that is
// measured in pixels, buttons, dialogs, scrolling lists and the progress and
// scanner layouts.
namespace ui {

constexpr float TopWidth = 400.0f;
constexpr float BottomWidth = 320.0f;
constexpr float ScreenHeight = 240.0f;

namespace color {
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
} // namespace color

struct Rect {
    float x;
    float y;
    float w;
    float h;
    bool contains(const touchPosition &touch) const;
};

bool init();
void shutdown();

// False once the system has asked 3dsDL to close. Use this instead of calling
// aptMainLoop() directly so every loop sees the request.
bool keepRunning();

struct Input {
    u32 down = 0;
    u32 repeat = 0;
    touchPosition touch {};
    bool tapped(const Rect &rect) const;
};
Input readInput();

// --- Drawing ---------------------------------------------------------------

void beginFrame(); // clears both screens and paints the top background
void beginTop();
void beginBottom();
void endFrame();

float textWidth(const std::string &value, float scale);
float lineHeight(float scale);
// Cuts value with "..." at the end so it fits maxWidth.
std::string fitEnd(const std::string &value, float scale, float maxWidth);
// Cuts value in the middle, keeping both ends: "sdmc:/a/...e/file.zip".
std::string fitMiddle(const std::string &value, float scale, float maxWidth);
// Breaks value into lines no wider than maxWidth, preferring spaces and URL
// separators. The last line ends in "..." when maxLines cuts text off.
std::vector<std::string> wrap(const std::string &value, float scale,
                              float maxWidth, size_t maxLines);

void drawText(const std::string &value, float x, float y, float scale,
              u32 textColor, u32 flags = 0);
// Draws wrapped text and returns the y coordinate below it.
float drawWrapped(const std::string &value, float x, float y, float scale,
                  u32 textColor, float maxWidth, size_t maxLines);
void drawBackground(float width);
void drawHeader(const std::string &subtitle);
void drawPanel(const Rect &rect, u32 fill = color::Surface);
void drawButton(const Rect &rect, const std::string &label, bool selected,
                bool destructive = false);
// fraction < 0 draws an indeterminate, moving bar.
void drawProgressBar(const Rect &rect, float fraction);

// Puts a 48x48 RGB565 icon (row by row, see smdh::Info) into the shared icon
// texture and returns it for drawing, or nullptr when pixels is not an icon.
// Only one icon can be shown at a time.
const C2D_Image *showIcon(const std::vector<uint16_t> &pixels);

// --- Dialogs ---------------------------------------------------------------

enum class Tone { Info, Success, Warning, Error };

struct Paragraph {
    std::string text;
    u32 textColor = color::Text;
    float scale = 0.5f;
    size_t maxLines = 3;
};

struct Dialog {
    std::string header; // short word at the top right, e.g. "confirm"
    std::string title;
    Tone tone = Tone::Info;
    std::vector<Paragraph> body;
    std::vector<std::string> buttons; // one to three
    int initial = 0;
    int dangerous = -1;    // button drawn in red
    int cancelButton = -1; // returned for B; -1 means B returns -1
    // A 48x48 picture at the top right, such as a title's icon.
    const C2D_Image *icon = nullptr;
};

// Shows dialog until a button is chosen. Returns its index, or -1 for B (when
// cancelButton is -1) or when the app is closing.
int choose(const Dialog &dialog);
void message(const std::string &title, const std::string &text,
             Tone tone = Tone::Info, const std::string &detail = "");

// --- Lists -----------------------------------------------------------------

struct ListItem {
    std::string label;
    std::string detail; // short text on the right
    u32 labelColor = color::Text;
};

struct ListSpec {
    std::string header; // top screen subtitle
    std::string title;  // bottom screen heading
    std::vector<ListItem> items;
    std::string emptyText;
    std::string hint;   // controls line at the bottom
    u32 extraKeys = 0;  // keys besides A and B that close the list
    // Draws the top screen for the highlighted item (index -1 when empty),
    // after the header has been drawn.
    std::function<void(int)> drawTop;
};

struct ListResult {
    u32 key = 0;    // KEY_A, KEY_B or one of extraKeys; 0 when closing
    int index = -1; // highlighted item
};

ListResult runList(const ListSpec &spec, int &cursor);

// --- Progress and scanner layouts ------------------------------------------

struct ProgressView {
    std::string header;
    std::string title;
    std::string subtitle;
    std::string note;
    float fraction = -1.0f;
    std::string amounts;
    std::string status;
    std::string speed;
    std::string folderLabel = "Saving to";
    std::string folder;
    std::string footer;
};

extern const Rect ProgressCancelButton;
void drawProgress(const ProgressView &view);

// A "please wait" frame with a moving bar.
void drawBusy(const std::string &header, const std::string &title);

extern const Rect ScannerCancelButton;
void drawScanner(const C2D_Image *preview, const std::string &status,
                 u32 statusColor);

} // namespace ui
