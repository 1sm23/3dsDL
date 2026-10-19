#include "../check.hpp"

#include "chime.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

void putLe(std::FILE *file, uint32_t value, int bytes)
{
    for (int index = 0; index < bytes; ++index) {
        std::fputc(static_cast<int>((value >> (8 * index)) & 0xff), file);
    }
}

// With CHIME_WAV_DIR set, the sounds are written there to listen to.
void writeWav(const std::string &path, const std::vector<int16_t> &samples)
{
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return;
    }
    const uint32_t bytes = static_cast<uint32_t>(samples.size() * 2);
    std::fputs("RIFF", file);
    putLe(file, 36 + bytes, 4);
    std::fputs("WAVEfmt ", file);
    putLe(file, 16, 4);
    putLe(file, 1, 2);
    putLe(file, 1, 2);
    putLe(file, chime::Rate, 4);
    putLe(file, chime::Rate * 2, 4);
    putLe(file, 2, 2);
    putLe(file, 16, 2);
    std::fputs("data", file);
    putLe(file, bytes, 4);
    for (const int16_t sample : samples) {
        putLe(file, static_cast<uint16_t>(sample), 2);
    }
    std::fclose(file);
}

} // namespace

TEST("chime: every sound is short, calm and free of clicks")
{
    const char *const Names[] = {"move", "select", "back", "error", "notify"};
    const char *folder = std::getenv("CHIME_WAV_DIR");
    for (int index = 0; index < chime::EffectCount; ++index) {
        const std::vector<int16_t> samples =
            chime::render(static_cast<chime::Effect>(index));
        CHECK(!samples.empty());
        const double seconds = static_cast<double>(samples.size()) / chime::Rate;
        CHECK(seconds > 0.02 && seconds < 0.8);

        int peak = 0;
        int biggestStep = 0;
        for (size_t at = 0; at < samples.size(); ++at) {
            peak = std::max(peak, std::abs(static_cast<int>(samples[at])));
            if (at > 0) {
                biggestStep = std::max(
                    biggestStep, std::abs(samples[at] - samples[at - 1]));
            }
        }
        // Calm: well below full scale.
        CHECK(peak > 2000 && peak < 32767 * 0.25);
        // No clicks: from one sample to the next the wave never jumps by
        // more than a smooth tone at this level could.
        CHECK(biggestStep < peak / 2);
        // It starts and ends in silence.
        CHECK(std::abs(samples.front()) < 40);
        CHECK(std::abs(samples.back()) < 40);

        if (folder != nullptr) {
            writeWav(std::string(folder) + "/" + Names[index] + ".wav", samples);
        }
    }
}
