#include "chime.hpp"

#include <algorithm>
#include <cmath>

namespace chime {

namespace {

constexpr double Pi = 3.14159265358979323846;

struct Note {
    double start;    // seconds from the start of the sound
    double hz;
    double length;   // seconds, including the tail
    double decay;    // seconds for the ring to fall to about a third
    double level;    // relative loudness
    double glide;    // pitch at the end as a share of the start (1 = none)
};

struct Voice {
    std::vector<Note> notes;
    double peak; // loudest sample, as a share of full scale
};

Voice design(Effect effect)
{
    switch (effect) {
    case Effect::Move:
        // A short, woody tick: low enough not to be shrill, and quiet.
        return {{{0.000, 1174.7, 0.045, 0.010, 1.0, 0.94}}, 0.10};
    case Effect::Select:
        // Two notes rising a fifth, the second ringing a little longer.
        return {{{0.000, 659.3, 0.160, 0.040, 0.8, 1.0},
                 {0.055, 987.8, 0.220, 0.055, 1.0, 1.0}},
                0.20};
    case Effect::Back:
        // The same, falling.
        return {{{0.000, 880.0, 0.160, 0.040, 0.9, 1.0},
                 {0.055, 587.3, 0.220, 0.055, 1.0, 1.0}},
                0.19};
    case Effect::Error:
        // Low and soft, two notes stepping down, never a buzz.
        return {{{0.000, 392.0, 0.200, 0.070, 1.0, 0.99},
                 {0.110, 311.1, 0.300, 0.090, 1.0, 0.99}},
                0.22};
    case Effect::Notify:
        // A gentle rising chord, each note left to ring.
        return {{{0.000, 784.0, 0.420, 0.110, 0.9, 1.0},
                 {0.075, 987.8, 0.420, 0.110, 0.9, 1.0},
                 {0.150, 1174.7, 0.520, 0.140, 1.0, 1.0}},
                0.18};
    }
    return {{}, 0};
}

// A soft bell: the fundamental, a quiet octave that fades faster (the
// "pluck" at the start), and an envelope with a smooth rise and a smooth
// fade to true silence, so no note starts or ends with a click.
double voice(const Note &note, double t)
{
    if (t < 0 || t >= note.length) {
        return 0;
    }
    constexpr double Attack = 0.004;
    constexpr double Release = 0.025;
    double envelope = std::exp(-t / note.decay);
    if (t < Attack) {
        envelope *= 0.5 - 0.5 * std::cos(Pi * t / Attack);
    }
    const double tail = note.length - t;
    if (tail < Release) {
        envelope *= 0.5 - 0.5 * std::cos(Pi * tail / Release);
    }
    // The pitch glides smoothly from hz to hz * glide; the phase is the
    // integral of the frequency, so the wave never jumps.
    const double rate = std::log(note.glide) / note.length;
    const double phase = std::abs(rate) < 1e-9
                             ? 2 * Pi * note.hz * t
                             : 2 * Pi * note.hz * (std::exp(rate * t) - 1) / rate;
    const double body = std::sin(phase);
    const double shine = 0.18 * std::exp(-t / (note.decay * 0.35)) *
                         std::sin(2 * phase);
    return note.level * envelope * (body + shine);
}

} // namespace

std::vector<int16_t> render(Effect effect, int rate)
{
    const Voice voiceDesign = design(effect);
    double end = 0;
    for (const Note &note : voiceDesign.notes) {
        end = std::max(end, note.start + note.length);
    }
    const size_t count = static_cast<size_t>(end * rate) + 1;
    std::vector<double> mixed(count, 0.0);
    for (size_t index = 0; index < count; ++index) {
        const double t = static_cast<double>(index) / rate;
        for (const Note &note : voiceDesign.notes) {
            mixed[index] += voice(note, t - note.start);
        }
    }

    // A gentle low-pass (two one-pole stages near 5 kHz) takes the edge off
    // without dulling the tone.
    const double cutoff = 5000.0;
    const double alpha = 1.0 - std::exp(-2 * Pi * cutoff / rate);
    double first = 0;
    double second = 0;
    double loudest = 0;
    for (double &sample : mixed) {
        first += alpha * (sample - first);
        second += alpha * (first - second);
        sample = second;
        loudest = std::max(loudest, std::abs(sample));
    }

    // Every sound at its own calm level, whatever the notes add up to.
    const double scale = loudest > 0 ? voiceDesign.peak / loudest : 0;
    std::vector<int16_t> out(count);
    for (size_t index = 0; index < count; ++index) {
        out[index] = static_cast<int16_t>(
            std::lround(std::max(-1.0, std::min(1.0, mixed[index] * scale)) *
                        32767));
    }
    return out;
}

} // namespace chime
