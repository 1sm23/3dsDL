#pragma once

#include <cstdint>
#include <string>

// A QR code of text, for phones to scan off the top screen.
class QrCode {
public:
    static constexpr int MaxVersion = 7;

    // maxVersion caps how many modules the code may have (version 7 is 45
    // across). compact allows the lowest error correction, which a code on
    // a screen can afford, so longer text fits; the encoder still raises it
    // when there is room.
    explicit QrCode(const std::string &text, int maxVersion = 6,
                    bool compact = false);
    bool ok() const { return ok_; }
    int size() const; // modules across, 0 when the text did not fit
    // Draws modules of px console pixels at x, y on white, with a quiet zone
    // of two modules around them.
    void draw(float x, float y, float px) const;

private:
    // qrcodegen_BUFFER_LEN_FOR_VERSION(MaxVersion)
    uint8_t code_[((MaxVersion * 4 + 17) * (MaxVersion * 4 + 17) + 7) / 8 + 1] =
        {};
    bool ok_ = false;
};
