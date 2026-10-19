#pragma once

// Short interface sounds in the spirit of the system applets: a tick when
// the cursor moves, blips for choosing and going back, a low tone for
// errors and a chime for notifications. They are made in code, so there
// are no audio files, and played through the DSP. Without the DSP firmware
// (sdmc:/3ds/dspfirm.cdc) 3dsDL is simply quiet.
namespace sound {

enum class Effect { Move, Select, Back, Error, Notify };

void init();
void exit();
// False when the DSP could not start.
bool available();
void setEnabled(bool enabled);
void play(Effect effect);

} // namespace sound
