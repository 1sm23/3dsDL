// Sound cues: {frame, kind, gain?, pan?}, frames at 60 fps from the start of
// the film (so seconds x 60). build.mjs mixes each one over the score with
// the app's own UI sounds (ported from chime.cpp) and the edit's sounds.
// Kinds: tap move select back chime done installed notify error type whoosh
// impact pop riser lid boot shutter. A riser peaks 2 bars after its frame.
export {CUES} from './cue-sheet.ts';
