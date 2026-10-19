#include "sound.hpp"

#include <3ds.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace sound {

namespace {

constexpr float Rate = 22050.0f;
constexpr float Pi = 3.14159265f;
constexpr int EffectCount = 5;
// One DSP channel per effect, so a quick move does not cut off a choice.
constexpr int FirstChannel = 8;

struct Note {
    float startHz;
    float endHz;
    float seconds;
    float volume;
};

struct Clip {
    int16_t *samples = nullptr;
    size_t count = 0;
    ndspWaveBuf buffer {};
};

bool started = false;
bool enabled = true;
Clip clips[EffectCount];

// Notes one after another: a sine glide with a soft attack and an
// exponential fade, quiet enough to sit under the system's own sounds.
std::vector<int16_t> render(const std::vector<Note> &notes)
{
    std::vector<int16_t> out;
    float phase = 0;
    for (const Note &note : notes) {
        const int length = static_cast<int>(note.seconds * Rate);
        for (int index = 0; index < length; ++index) {
            const float t = static_cast<float>(index) / length;
            const float hz = note.startHz + (note.endHz - note.startHz) * t;
            phase += 2 * Pi * hz / Rate;
            const float attack = std::min(1.0f, index / (Rate * 0.004f));
            const float fade = std::exp(-4.5f * t);
            // A touch of the octave makes it sound less like a test tone.
            const float wave = std::sin(phase) * 0.8f + std::sin(2 * phase) * 0.2f;
            out.push_back(static_cast<int16_t>(wave * attack * fade *
                                               note.volume * 32767));
        }
    }
    return out;
}

std::vector<int16_t> design(Effect effect)
{
    switch (effect) {
    case Effect::Move:
        return render({{1760, 1760, 0.028f, 0.22f}});
    case Effect::Select:
        return render({{1046, 1046, 0.045f, 0.32f}, {1568, 1568, 0.07f, 0.32f}});
    case Effect::Back:
        return render({{1318, 1318, 0.045f, 0.28f}, {880, 880, 0.07f, 0.28f}});
    case Effect::Error:
        return render({{330, 300, 0.09f, 0.35f}, {262, 240, 0.14f, 0.35f}});
    case Effect::Notify:
        return render({{1046, 1046, 0.08f, 0.26f},
                       {1318, 1318, 0.08f, 0.26f},
                       {1568, 1568, 0.22f, 0.26f}});
    }
    return {};
}

} // namespace

void init()
{
    if (started || R_FAILED(ndspInit())) {
        return;
    }
    started = true;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int index = 0; index < EffectCount; ++index) {
        const std::vector<int16_t> pcm = design(static_cast<Effect>(index));
        Clip &clip = clips[index];
        clip.count = pcm.size();
        clip.samples =
            static_cast<int16_t *>(linearAlloc(pcm.size() * sizeof(int16_t)));
        if (clip.samples == nullptr) {
            clip.count = 0;
            continue;
        }
        std::memcpy(clip.samples, pcm.data(), pcm.size() * sizeof(int16_t));
        DSP_FlushDataCache(clip.samples, pcm.size() * sizeof(int16_t));

        const int channel = FirstChannel + index;
        ndspChnReset(channel);
        ndspChnSetInterp(channel, NDSP_INTERP_LINEAR);
        ndspChnSetRate(channel, Rate);
        ndspChnSetFormat(channel, NDSP_FORMAT_MONO_PCM16);
        float mix[12] = {};
        mix[0] = mix[1] = 0.8f;
        ndspChnSetMix(channel, mix);
        clip.buffer.data_pcm16 = clip.samples;
        clip.buffer.nsamples = static_cast<u32>(clip.count);
        clip.buffer.looping = false;
    }
}

void exit()
{
    if (!started) {
        return;
    }
    for (int index = 0; index < EffectCount; ++index) {
        ndspChnWaveBufClear(FirstChannel + index);
    }
    ndspExit();
    for (Clip &clip : clips) {
        if (clip.samples != nullptr) {
            linearFree(clip.samples);
        }
        clip = Clip();
    }
    started = false;
}

bool available()
{
    return started;
}

void setEnabled(bool value)
{
    enabled = value;
}

void play(Effect effect)
{
    if (!started || !enabled) {
        return;
    }
    const int index = static_cast<int>(effect);
    Clip &clip = clips[index];
    if (clip.count == 0) {
        return;
    }
    const int channel = FirstChannel + index;
    ndspChnWaveBufClear(channel);
    clip.buffer.status = NDSP_WBUF_FREE;
    ndspChnWaveBufAdd(channel, &clip.buffer);
}

} // namespace sound
