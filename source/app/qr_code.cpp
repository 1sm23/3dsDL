#include "qr_code.hpp"

#include "ui.hpp"

extern "C" {
#include "qrcodegen.h"
}

#include <algorithm>

QrCode::QrCode(const std::string &text, int maxVersion, bool compact)
{
    static_assert(sizeof(code_) ==
                      qrcodegen_BUFFER_LEN_FOR_VERSION(MaxVersion),
                  "the buffer must hold the largest version");
    uint8_t temp[sizeof(code_)];
    ok_ = qrcodegen_encodeText(text.c_str(), temp, code_,
                               compact ? qrcodegen_Ecc_LOW
                                       : qrcodegen_Ecc_MEDIUM,
                               qrcodegen_VERSION_MIN,
                               std::clamp(maxVersion, 1, MaxVersion),
                               qrcodegen_Mask_AUTO, true);
}

int QrCode::size() const
{
    return ok_ ? qrcodegen_getSize(code_) : 0;
}

void QrCode::draw(float x, float y, float px) const
{
    if (!ok_) {
        return;
    }
    const int count = size();
    const float quiet = 2 * px;
    ui::fillRound({x - quiet, y - quiet, count * px + 2 * quiet,
                   count * px + 2 * quiet},
                  4, ui::color::rgb(0xFFFFFF));
    for (int row = 0; row < count; ++row) {
        for (int column = 0; column < count; ++column) {
            if (qrcodegen_getModule(code_, column, row)) {
                ui::fillRound({x + column * px, y + row * px, px, px}, 0,
                              ui::color::rgb(0x222222));
            }
        }
    }
}
