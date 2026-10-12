#include "ui.hpp"

#include "text.hpp"

#include <algorithm>
#include <cmath>

namespace ui {
namespace {

C3D_RenderTarget *topTarget = nullptr;
C3D_RenderTarget *bottomTarget = nullptr;
C2D_TextBuf textBuffer = nullptr;
bool closing = false;

// citro2d draws the system font at 30 / cellHeight; matching that keeps the
// measurements below identical to what ends up on screen.
float fontScale = 1.0f;
float fontLineFeed = 30.0f;
float asciiAdvance[128];
bool asciiReady = false;

constexpr const char *Ellipsis = "...";

void loadFontMetrics()
{
    CFNT_s *font = fontGetSystemFont();
    fontScale = 30.0f / fontGetGlyphInfo(font)->cellHeight;
    fontLineFeed = fontGetInfo(font)->lineFeed;
}

float advance(uint32_t codePoint)
{
    if (codePoint < 128 && asciiReady) {
        return asciiAdvance[codePoint];
    }
    fontGlyphPos_s glyph;
    C2D_FontCalcGlyphPos(nullptr, &glyph,
                         C2D_FontGlyphIndexFromCodePoint(nullptr, codePoint),
                         0, 1.0f, 1.0f);
    return glyph.xAdvance * fontScale;
}

void prepareAscii()
{
    for (uint32_t codePoint = 0; codePoint < 128; ++codePoint) {
        asciiAdvance[codePoint] = advance(codePoint);
    }
    asciiReady = true;
}

struct Glyph {
    size_t offset;
    size_t length;
    uint32_t codePoint;
    float width;
};

std::vector<Glyph> glyphs(const std::string &value, float scale)
{
    std::vector<Glyph> output;
    output.reserve(value.size());
    for (size_t index = 0; index < value.size();) {
        uint32_t codePoint = 0xfffd;
        size_t length = text::utf8SequenceLength(value, index, &codePoint);
        if (length == 0) {
            length = 1;
            codePoint = 0xfffd;
        }
        output.push_back({index, length, codePoint,
                          advance(codePoint) * scale});
        index += length;
    }
    return output;
}

bool isBreakAfter(uint32_t codePoint)
{
    return codePoint == ' ' || codePoint == '/' || codePoint == '?' ||
           codePoint == '&' || codePoint == '=' || codePoint == '-' ||
           codePoint == '_' || codePoint == '.' || codePoint == ',';
}

std::string rtrimSpaces(std::string value)
{
    while (!value.empty() && value.back() == ' ') {
        value.pop_back();
    }
    return value;
}

// Removes characters from the end until value + "..." fits.
std::string withEllipsis(const std::string &value, float scale,
                         float maxWidth)
{
    const float ellipsisWidth = textWidth(Ellipsis, scale);
    const std::vector<Glyph> all = glyphs(value, scale);
    float width = 0.0f;
    size_t end = 0;
    for (const Glyph &glyph : all) {
        if (width + glyph.width + ellipsisWidth > maxWidth) {
            break;
        }
        width += glyph.width;
        end = glyph.offset + glyph.length;
    }
    return rtrimSpaces(value.substr(0, end)) + Ellipsis;
}

void drawTopForDialog(const Dialog &dialog)
{
    drawHeader(dialog.header);
    const Rect panel = {18, 54, 364, 172};
    drawPanel(panel);

    u32 titleColor = color::Text;
    switch (dialog.tone) {
    case Tone::Success:
        titleColor = color::Success;
        break;
    case Tone::Warning:
        titleColor = color::Warning;
        break;
    case Tone::Error:
        titleColor = color::Danger;
        break;
    case Tone::Info:
        break;
    }

    float y = drawWrapped(dialog.title, 34, 64, 0.68f, titleColor, 334, 2) + 6;
    const float bottom = panel.y + panel.h - 6;
    for (const Paragraph &paragraph : dialog.body) {
        if (paragraph.text.empty()) {
            continue;
        }
        const size_t room = static_cast<size_t>(
            std::max(0.0f, (bottom - y) / lineHeight(paragraph.scale)));
        const size_t lines = std::min(paragraph.maxLines, room);
        if (lines == 0) {
            break;
        }
        y = drawWrapped(paragraph.text, 34, y, paragraph.scale,
                        paragraph.textColor, 334, lines) + 5;
    }
}

std::vector<Rect> dialogButtons(size_t count)
{
    std::vector<Rect> rects;
    if (count <= 1) {
        rects.push_back({58, 92, 204, 52});
        return rects;
    }
    const float gap = 8.0f;
    const float width = (296.0f - gap * (count - 1)) / count;
    for (size_t index = 0; index < count; ++index) {
        rects.push_back({12 + index * (width + gap), 92, width, 52});
    }
    return rects;
}

} // namespace

const Rect ProgressCancelButton = {58, 137, 204, 50};
const Rect ScannerCancelButton = {58, 159, 204, 48};

bool Rect::contains(const touchPosition &touch) const
{
    return touch.px >= x && touch.px <= x + w && touch.py >= y &&
           touch.py <= y + h;
}

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

    if (topTarget == nullptr || bottomTarget == nullptr ||
        textBuffer == nullptr) {
        shutdown();
        return false;
    }

    // C2D_TextBufNew maps the system font, so its metrics are available now.
    loadFontMetrics();
    prepareAscii();
    hidSetRepeatParameters(20, 6);
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

bool keepRunning()
{
    if (!closing && !aptMainLoop()) {
        closing = true;
    }
    return !closing;
}

bool Input::tapped(const Rect &rect) const
{
    return (down & KEY_TOUCH) != 0 && rect.contains(touch);
}

Input readInput()
{
    hidScanInput();
    Input input;
    input.down = hidKeysDown();
    input.repeat = hidKeysDownRepeat();
    hidTouchRead(&input.touch);
    return input;
}

void beginFrame()
{
    C2D_TextBufClear(textBuffer);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(topTarget, color::Background);
    C2D_TargetClear(bottomTarget, color::Background);
    C2D_SceneBegin(topTarget);
    drawBackground(TopWidth);
}

void beginTop()
{
    C2D_SceneBegin(topTarget);
}

void beginBottom()
{
    C2D_SceneBegin(bottomTarget);
}

void endFrame()
{
    C3D_FrameEnd(0);
}

float textWidth(const std::string &value, float scale)
{
    float width = 0.0f;
    for (const Glyph &glyph : glyphs(value, scale)) {
        width += glyph.width;
    }
    return width;
}

float lineHeight(float scale)
{
    return std::ceil(scale * fontScale * fontLineFeed);
}

std::string fitEnd(const std::string &value, float scale, float maxWidth)
{
    if (textWidth(value, scale) <= maxWidth) {
        return value;
    }
    return withEllipsis(value, scale, maxWidth);
}

std::string fitMiddle(const std::string &value, float scale, float maxWidth)
{
    if (textWidth(value, scale) <= maxWidth) {
        return value;
    }
    const std::vector<Glyph> all = glyphs(value, scale);
    const float budget = maxWidth - textWidth(Ellipsis, scale);
    if (budget <= 0.0f || all.empty()) {
        return Ellipsis;
    }

    float headWidth = 0.0f;
    size_t head = 0;
    while (head < all.size() && headWidth + all[head].width <= budget * 0.4f) {
        headWidth += all[head].width;
        ++head;
    }
    float tailWidth = 0.0f;
    size_t tail = all.size();
    while (tail > head &&
           headWidth + tailWidth + all[tail - 1].width <= budget) {
        tailWidth += all[tail - 1].width;
        --tail;
    }

    const size_t headEnd = head == 0 ? 0 : all[head - 1].offset +
                                               all[head - 1].length;
    const size_t tailStart = tail < all.size() ? all[tail].offset
                                               : value.size();
    return value.substr(0, headEnd) + Ellipsis + value.substr(tailStart);
}

std::vector<std::string> wrap(const std::string &value, float scale,
                              float maxWidth, size_t maxLines)
{
    std::vector<std::string> lines;
    if (maxLines == 0) {
        return lines;
    }

    const std::vector<Glyph> all = glyphs(value, scale);
    std::string line;
    float width = 0.0f;
    size_t breakAt = 0; // bytes of line up to the last break opportunity
    bool cut = false;

    const auto push = [&](const std::string &finished) {
        if (lines.size() == maxLines) {
            cut = true;
            return;
        }
        lines.push_back(rtrimSpaces(finished));
    };

    for (const Glyph &glyph : all) {
        if (cut) {
            break;
        }
        if (glyph.codePoint == '\n') {
            push(line);
            line.clear();
            width = 0.0f;
            breakAt = 0;
            continue;
        }
        if (glyph.codePoint == ' ' && line.empty()) {
            continue;
        }

        if (width + glyph.width > maxWidth && !line.empty()) {
            std::string rest;
            if (breakAt > 0 && breakAt < line.size()) {
                rest = line.substr(breakAt);
                line.erase(breakAt);
            }
            push(line);
            line = rest;
            while (!line.empty() && line.front() == ' ') {
                line.erase(line.begin());
            }
            width = textWidth(line, scale);
            breakAt = 0;
            if (cut) {
                break;
            }
            if (glyph.codePoint == ' ' && line.empty()) {
                continue;
            }
        }

        line.append(value, glyph.offset, glyph.length);
        width += glyph.width;
        if (isBreakAfter(glyph.codePoint)) {
            breakAt = line.size();
        }
    }
    if (!cut && !line.empty()) {
        push(line);
    }

    if (cut && !lines.empty()) {
        lines.back() = withEllipsis(lines.back(), scale, maxWidth);
    }
    return lines;
}

void drawText(const std::string &value, float x, float y, float scale,
              u32 textColor, u32 flags)
{
    C2D_Text textObject;
    C2D_TextParse(&textObject, textBuffer, value.c_str());
    C2D_TextOptimize(&textObject);
    C2D_DrawText(&textObject, flags | C2D_WithColor, x, y, 0.8f, scale,
                 scale, textColor);
}

float drawWrapped(const std::string &value, float x, float y, float scale,
                  u32 textColor, float maxWidth, size_t maxLines)
{
    const float step = lineHeight(scale);
    for (const std::string &line : wrap(value, scale, maxWidth, maxLines)) {
        drawText(line, x, y, scale, textColor);
        y += step;
    }
    return y;
}

void drawBackground(float width)
{
    C2D_DrawRectangle(0, 0, 0.0f, width, ScreenHeight, color::Background,
                      color::Background2, color::Background,
                      color::Background2);
}

void drawHeader(const std::string &subtitle)
{
    drawText("3dsDL", 18, 13, 0.8f, color::Text);
    C2D_DrawRectSolid(18, 39, 0.4f, 42, 3, color::Primary);
    if (!subtitle.empty()) {
        drawText(fitEnd(subtitle, 0.6f, 260), 382, 18, 0.6f, color::Muted,
                 C2D_AlignRight);
    }
}

void drawPanel(const Rect &rect, u32 fill)
{
    C2D_DrawRectSolid(rect.x + 2, rect.y + 3, 0.1f, rect.w, rect.h,
                      color::Shadow);
    C2D_DrawRectSolid(rect.x, rect.y, 0.2f, rect.w, rect.h, fill);
    C2D_DrawRectSolid(rect.x, rect.y, 0.3f, 4, rect.h, color::PrimaryDark);
}

void drawButton(const Rect &rect, const std::string &label, bool selected,
                bool destructive)
{
    const u32 base = selected ? (destructive ? color::Danger : color::Primary)
                              : color::SurfaceRaised;
    const u32 labelColor = selected ? color::Background : color::Text;

    C2D_DrawRectSolid(rect.x + 2, rect.y + 3, 0.3f, rect.w, rect.h,
                      color::Shadow);
    C2D_DrawRectSolid(rect.x, rect.y, 0.4f, rect.w, rect.h, base);
    if (selected) {
        C2D_DrawRectSolid(rect.x, rect.y, 0.5f, 5, rect.h,
                          destructive ? C2D_Color32(0xff, 0xb0, 0xbc, 0xff)
                                      : C2D_Color32(0xc5, 0xf8, 0xfc, 0xff));
    } else if (destructive) {
        C2D_DrawRectSolid(rect.x, rect.y + rect.h - 3, 0.5f, rect.w, 3,
                          color::Danger);
    }

    const float scale = 0.58f;
    drawText(fitEnd(label, scale, rect.w - 12), rect.x + rect.w / 2,
             rect.y + rect.h / 2 - lineHeight(scale) / 2, scale, labelColor,
             C2D_AlignCenter);
}

void drawProgressBar(const Rect &rect, float fraction)
{
    C2D_DrawRectSolid(rect.x, rect.y, 0.3f, rect.w, rect.h,
                      color::SurfaceRaised);
    if (fraction >= 0.0f) {
        C2D_DrawRectSolid(rect.x, rect.y, 0.4f,
                          rect.w * std::min(1.0f, fraction), rect.h,
                          color::Primary);
        return;
    }
    const float segment = rect.w * 0.2f;
    const float travel = rect.w - segment;
    const float position = static_cast<float>(
        (osGetTime() / 6) % static_cast<u64>(std::max(1.0f, travel)));
    C2D_DrawRectSolid(rect.x + position, rect.y, 0.4f, segment, rect.h,
                      color::Primary);
}

int choose(const Dialog &dialog)
{
    const std::vector<std::string> labels =
        dialog.buttons.empty() ? std::vector<std::string>{"OK"}
                               : dialog.buttons;
    const std::vector<Rect> rects = dialogButtons(labels.size());
    const int count = static_cast<int>(labels.size());
    int selection = std::max(0, std::min(dialog.initial, count - 1));

    while (keepRunning()) {
        const Input input = readInput();
        if (input.repeat & KEY_LEFT) {
            selection = (selection + count - 1) % count;
        }
        if (input.repeat & KEY_RIGHT) {
            selection = (selection + 1) % count;
        }
        if (input.down & KEY_B) {
            return dialog.cancelButton;
        }
        if (input.down & KEY_A) {
            return selection;
        }
        for (int index = 0; index < count; ++index) {
            if (input.tapped(rects[index])) {
                return index;
            }
        }

        beginFrame();
        drawTopForDialog(dialog);

        beginBottom();
        drawText(count > 1 ? "Choose with Left/Right or touch"
                           : "Press A or touch to continue",
                 18, 22, 0.48f, color::Muted);
        for (int index = 0; index < count; ++index) {
            drawButton(rects[index], labels[index], index == selection,
                       index == dialog.dangerous);
        }
        drawText(dialog.cancelButton >= 0 || count == 1 ? "A: Select    B: Back"
                                                        : "A: Select",
                 160, 196, 0.45f, color::Muted, C2D_AlignCenter);
        endFrame();
    }
    return -1;
}

void message(const std::string &title, const std::string &text, Tone tone,
             const std::string &detail)
{
    Dialog dialog;
    dialog.header = tone == Tone::Error     ? "error"
                  : tone == Tone::Success   ? "done"
                  : tone == Tone::Warning   ? "heads up"
                                            : "info";
    dialog.title = title;
    dialog.tone = tone;
    dialog.body.push_back({text, color::Text, 0.5f, 6});
    if (!detail.empty()) {
        dialog.body.push_back({detail, color::Muted, 0.42f, 3});
    }
    dialog.buttons = {"OK"};
    dialog.cancelButton = 0;
    choose(dialog);
}

ListResult runList(const ListSpec &spec, int &cursor)
{
    constexpr float Top = 34.0f;
    constexpr float RowHeight = 22.0f;
    constexpr int VisibleRows = 8;
    const Rect scrollUp = {296, Top, 24, RowHeight * VisibleRows / 2};
    const Rect scrollDown = {296, Top + RowHeight * VisibleRows / 2, 24,
                             RowHeight * VisibleRows / 2};

    const int count = static_cast<int>(spec.items.size());
    cursor = count == 0 ? -1 : std::max(0, std::min(cursor, count - 1));
    int first = std::max(0, cursor - VisibleRows / 2);

    while (keepRunning()) {
        const Input input = readInput();
        if (count > 0) {
            if (input.repeat & KEY_UP) {
                cursor = (cursor + count - 1) % count;
            }
            if (input.repeat & KEY_DOWN) {
                cursor = (cursor + 1) % count;
            }
            if (input.repeat & (KEY_LEFT | KEY_L) || input.tapped(scrollUp)) {
                cursor = std::max(0, cursor - VisibleRows);
            }
            if (input.repeat & (KEY_RIGHT | KEY_R) ||
                input.tapped(scrollDown)) {
                cursor = std::min(count - 1, cursor + VisibleRows);
            }
        }
        if (input.down & KEY_B) {
            return {KEY_B, cursor};
        }
        if ((input.down & KEY_A) && count > 0) {
            return {KEY_A, cursor};
        }
        if (input.down & spec.extraKeys) {
            return {input.down & spec.extraKeys, cursor};
        }
        if ((input.down & KEY_TOUCH) && input.touch.px < 292) {
            const int row = static_cast<int>((input.touch.py - Top) /
                                             RowHeight);
            const int tapped = first + row;
            if (input.touch.py >= Top && row < VisibleRows &&
                tapped < count) {
                if (tapped == cursor) {
                    return {KEY_A, cursor};
                }
                cursor = tapped;
            }
        }

        if (cursor >= 0) {
            if (cursor < first) {
                first = cursor;
            } else if (cursor >= first + VisibleRows) {
                first = cursor - VisibleRows + 1;
            }
        }

        beginFrame();
        drawHeader(spec.header);
        if (spec.drawTop) {
            spec.drawTop(cursor);
        }

        beginBottom();
        drawText(fitEnd(spec.title, 0.56f, 290), 12, 9, 0.56f, color::Text);
        if (count == 0) {
            drawWrapped(spec.emptyText, 16, 70, 0.5f, color::Muted, 288, 4);
        }
        for (int row = 0; row < VisibleRows && first + row < count; ++row) {
            const int index = first + row;
            const ListItem &item = spec.items[index];
            const float y = Top + row * RowHeight;
            if (index == cursor) {
                C2D_DrawRectSolid(4, y, 0.3f, 288, RowHeight - 2,
                                  color::SurfaceRaised);
                C2D_DrawRectSolid(4, y, 0.4f, 4, RowHeight - 2,
                                  color::Primary);
            }
            const float detailWidth =
                item.detail.empty()
                    ? 0.0f
                    : std::min(110.0f, textWidth(item.detail, 0.44f)) + 8;
            drawText(fitEnd(item.label, 0.5f, 270 - detailWidth), 14, y + 3,
                     0.5f, item.labelColor);
            if (!item.detail.empty()) {
                drawText(fitEnd(item.detail, 0.44f, 110), 286, y + 4, 0.44f,
                         color::Muted, C2D_AlignRight);
            }
        }
        if (count > VisibleRows) {
            const float trackHeight = RowHeight * VisibleRows;
            const float thumb = std::max(12.0f, trackHeight * VisibleRows /
                                                    count);
            const float offset = (trackHeight - thumb) * first /
                                 std::max(1, count - VisibleRows);
            C2D_DrawRectSolid(306, Top, 0.3f, 6, trackHeight,
                              color::SurfaceRaised);
            C2D_DrawRectSolid(306, Top + offset, 0.4f, 6, thumb,
                              color::Primary);
        }
        drawText(fitEnd(spec.hint, 0.42f, 300), 160, 218, 0.42f, color::Muted,
                 C2D_AlignCenter);
        endFrame();
    }
    return {0, cursor};
}

void drawProgress(const ProgressView &view)
{
    beginFrame();
    drawHeader(view.header);

    drawPanel({18, 54, 364, 84});
    drawText(fitMiddle(view.title, 0.68f, 332), 34, 64, 0.68f, color::Text);
    drawText(fitEnd(view.subtitle, 0.46f, 332), 34, 94, 0.46f, color::Muted);
    if (!view.note.empty()) {
        drawText(fitEnd(view.note, 0.44f, 332), 34, 113, 0.44f,
                 color::Success);
    }

    drawProgressBar({24, 150, 352, 16}, view.fraction);
    drawText(view.amounts, 24, 174, 0.52f, color::Text);
    drawText(fitEnd(view.status, 0.48f, 150), 376, 175, 0.48f, color::Muted,
             C2D_AlignRight);
    drawText(view.speed, 24, 198, 0.48f, color::Muted);

    beginBottom();
    drawText("Saving to", 18, 14, 0.48f, color::Muted);
    drawPanel({18, 38, 284, 54});
    drawText(fitMiddle(view.folder, 0.52f, 256), 32, 55, 0.52f, color::Text);
    drawButton(ProgressCancelButton, "CANCEL", false, true);
    drawText(fitEnd(view.footer, 0.42f, 300), 160, 204, 0.42f, color::Muted,
             C2D_AlignCenter);
    drawText("B: Cancel", 160, 222, 0.42f, color::Muted, C2D_AlignCenter);
    endFrame();
}

void drawBusy(const std::string &header, const std::string &title)
{
    beginFrame();
    drawHeader(header);
    drawPanel({18, 70, 364, 100});
    drawWrapped(title, 34, 84, 0.6f, color::Text, 330, 2);
    drawProgressBar({34, 138, 332, 12}, -1.0f);
    beginBottom();
    drawText("Please wait...", 160, 110, 0.55f, color::Muted,
             C2D_AlignCenter);
    endFrame();
}

void drawScanner(const C2D_Image *preview, const std::string &status,
                 u32 statusColor)
{
    beginFrame();
    if (preview != nullptr) {
        C2D_DrawImageAt(*preview, 0, 0, 0.4f);
    }

    const u32 shade = C2D_Color32(0x00, 0x00, 0x00, 0x78);
    C2D_DrawRectSolid(0, 0, 0.5f, TopWidth, 34, shade);
    drawText("POINT THE CAMERA AT A QR CODE", 200, 8, 0.5f, color::Text,
             C2D_AlignCenter);

    const float left = 126;
    const float top = 46;
    const float right = 274;
    const float bottom = 194;
    const float line = 4;
    const float arm = 25;
    C2D_DrawRectSolid(left, top, 0.6f, arm, line, color::Primary);
    C2D_DrawRectSolid(left, top, 0.6f, line, arm, color::Primary);
    C2D_DrawRectSolid(right - arm, top, 0.6f, arm, line, color::Primary);
    C2D_DrawRectSolid(right - line, top, 0.6f, line, arm, color::Primary);
    C2D_DrawRectSolid(left, bottom - line, 0.6f, arm, line, color::Primary);
    C2D_DrawRectSolid(left, bottom - arm, 0.6f, line, arm, color::Primary);
    C2D_DrawRectSolid(right - arm, bottom - line, 0.6f, arm, line,
                      color::Primary);
    C2D_DrawRectSolid(right - line, bottom - arm, 0.6f, line, arm,
                      color::Primary);

    beginBottom();
    drawBackground(BottomWidth);
    drawText("QR SCANNER", 18, 17, 0.7f, color::Text);
    C2D_DrawRectSolid(18, 44, 0.4f, 52, 3, color::Primary);
    drawPanel({18, 62, 284, 80});
    drawWrapped(status, 34, 72, 0.52f, statusColor, 256, 2);
    drawWrapped("Hold a QR code with a direct http:// or https:// link "
                "steady in the frame.",
                34, 106, 0.42f, color::Muted, 256, 2);
    drawButton(ScannerCancelButton, "CANCEL", false);
    drawText("B: Cancel", 160, 216, 0.42f, color::Muted, C2D_AlignCenter);
    endFrame();
}

} // namespace ui
