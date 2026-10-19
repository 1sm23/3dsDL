#include "sound.hpp"

#include "chime.hpp"

#include <3ds.h>

#include <cstring>

namespace sound {

namespace {

// Two DSP channels per sound, used in turn, so a sound that plays again
// never cuts the last one off mid-wave (which clicks).
constexpr int ChannelsPerEffect = 2;
constexpr int FirstChannel = 8;
// Holding the D-pad repeats the cursor quickly; the tick keeps up only
// this often, so it never turns into a buzz.
constexpr u64 MoveSpacingMs = 55;

struct Clip {
    int16_t *samples = nullptr;
    size_t count = 0;
    ndspWaveBuf buffers[ChannelsPerEffect] {};
    int next = 0;
};

bool started = false;
bool enabled = true;
Clip clips[chime::EffectCount];
u64 lastMove = 0;

} // namespace

void init()
{
    if (started || R_FAILED(ndspInit())) {
        return;
    }
    started = true;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int index = 0; index < chime::EffectCount; ++index) {
        const std::vector<int16_t> pcm =
            chime::render(static_cast<chime::Effect>(index));
        Clip &clip = clips[index];
        const size_t bytes = pcm.size() * sizeof(int16_t);
        clip.samples = static_cast<int16_t *>(linearAlloc(bytes));
        if (clip.samples == nullptr) {
            continue;
        }
        clip.count = pcm.size();
        std::memcpy(clip.samples, pcm.data(), bytes);
        DSP_FlushDataCache(clip.samples, bytes);

        for (int lane = 0; lane < ChannelsPerEffect; ++lane) {
            const int channel = FirstChannel + index * ChannelsPerEffect + lane;
            ndspChnReset(channel);
            ndspChnSetInterp(channel, NDSP_INTERP_POLYPHASE);
            ndspChnSetRate(channel, static_cast<float>(chime::Rate));
            ndspChnSetFormat(channel, NDSP_FORMAT_MONO_PCM16);
            float mix[12] = {};
            mix[0] = mix[1] = 1.0f;
            ndspChnSetMix(channel, mix);
            ndspWaveBuf &buffer = clip.buffers[lane];
            buffer.data_pcm16 = clip.samples;
            buffer.nsamples = static_cast<u32>(clip.count);
            buffer.looping = false;
            buffer.status = NDSP_WBUF_DONE;
        }
    }
}

void exit()
{
    if (!started) {
        return;
    }
    for (int channel = 0; channel < chime::EffectCount * ChannelsPerEffect;
         ++channel) {
        ndspChnWaveBufClear(FirstChannel + channel);
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
    if (effect == Effect::Move) {
        const u64 now = osGetTime();
        if (now - lastMove < MoveSpacingMs) {
            return;
        }
        lastMove = now;
    }
    const int index = static_cast<int>(effect);
    Clip &clip = clips[index];
    if (clip.count == 0) {
        return;
    }
    // The lane that finished longest ago; a lane still playing is only
    // reused when both are, and then the older one gives way.
    const int lane = clip.next;
    clip.next = (clip.next + 1) % ChannelsPerEffect;
    const int channel = FirstChannel + index * ChannelsPerEffect + lane;
    ndspWaveBuf &buffer = clip.buffers[lane];
    if (buffer.status == NDSP_WBUF_QUEUED || buffer.status == NDSP_WBUF_PLAYING) {
        ndspChnWaveBufClear(channel);
    }
    buffer.status = NDSP_WBUF_FREE;
    ndspChnWaveBufAdd(channel, &buffer);
}

} // namespace sound
