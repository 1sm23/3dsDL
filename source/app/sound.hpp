#pragma once

// Short interface sounds in the spirit of the system applets: a soft tick
// when the cursor moves, bell notes for choosing and going back, a low pair
// for errors and a chord for notifications. core/chime makes them in code,
// so there are no audio files; they play through the DSP. Without the DSP
// firmware (sdmc:/3ds/dspfirm.cdc) 3dsDL is simply quiet.
namespace sound {

enum class Effect { Move, Select, Back, Error, Notify };

void init();
void exit();
// False when the DSP could not start.
bool available();
void setEnabled(bool enabled);
void play(Effect effect);

} // namespace sound
