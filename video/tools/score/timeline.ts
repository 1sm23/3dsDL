// The one place the film's timing lives. 120 BPM, so a beat is 0.5 s and a
// bar is 2 s. Scenes start on bar lines so the cuts land on the music; the
// score (build.mjs, score.mjs) reads this file, and so do the cue frames in
// cues.ts. Keep STORYBOARD.md and index.html in step with it.

export const FPS = 60; // cue resolution only; the video renders at 30
export const BPM = 120;
export const BEATS_PER_BAR = 4;
export const FRAMES_PER_BEAT = (FPS * 60) / BPM; // 30
export const FRAMES_PER_BAR = FRAMES_PER_BEAT * BEATS_PER_BAR; // 120

export type Music = 'intro' | 'build' | 'drop' | 'card' | 'groove' | 'break' | 'lift' | 'peak' | 'end';
export type Scene = {id: string; bar: number; bars: number; music: Music};

export const SCENES: Scene[] = [
  {id: 'open', bar: 0, bars: 3, music: 'intro'},
  {id: 'reveal', bar: 3, bars: 1, music: 'build'},
  {id: 'title', bar: 4, bars: 2, music: 'drop'},
  {id: 'home', bar: 6, bars: 2, music: 'groove'},
  {id: 'scan', bar: 8, bars: 4, music: 'groove'},
  {id: 'faster', bar: 12, bars: 2, music: 'break'},
  {id: 'install', bar: 14, bars: 2, music: 'groove'},
  {id: 'qol', bar: 16, bars: 2, music: 'lift'},
  {id: 'files', bar: 18, bars: 4, music: 'lift'},
  {id: 'titles', bar: 22, bars: 2, music: 'lift'},
  {id: 'receive', bar: 24, bars: 2, music: 'lift'},
  {id: 'wifi', bar: 26, bars: 5, music: 'lift'},
  {id: 'more', bar: 31, bars: 2, music: 'peak'},
  {id: 'versus', bar: 33, bars: 2, music: 'peak'},
  {id: 'end', bar: 35, bars: 4, music: 'end'},
];

export const TOTAL_BARS = SCENES.reduce((n, s) => Math.max(n, s.bar + s.bars), 0);
export const TOTAL_FRAMES = TOTAL_BARS * FRAMES_PER_BAR;
export const WIDTH = 1920;
export const HEIGHT = 1080;
