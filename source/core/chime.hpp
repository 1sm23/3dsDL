#pragma once

#include <cstdint>
#include <vector>

// The interface sounds, made in code: soft, rounded bell tones in the
// manner of the system menus. Kept free of libctru so the host tests can
// check them for clicks and write them out to listen to.
namespace chime {

enum class Effect { Move, Select, Back, Error, Notify };
constexpr int EffectCount = 5;

// The DSP's own rate, so the sounds are not resampled on the way out.
constexpr int Rate = 32728;

// Mono 16-bit samples of effect at rate, starting and ending in silence.
std::vector<int16_t> render(Effect effect, int rate = Rate);

} // namespace chime
