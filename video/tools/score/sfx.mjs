// Sound effects for the cue list. The interface sounds are the app's own:
// source/core/chime.cpp ported line for line (same notes, same bell voice,
// same 5 kHz two-pole smoothing and per-sound peak), rendered at 48 kHz.
// The rest (whoosh, impact, pop, riser, lid, boot, shutter, type) are
// designed here, short and in the soundtrack's key.

import {SR, TAU, midiHz, makeRng, SVF, smooth, clamp} from './dsp.mjs';
import {emit2, panL, panR, impact, riser} from './instruments.mjs';

// ------------------------------------------------------ chime.cpp port

// {start, hz, length, decay, level, glide}, plus the loudest sample.
const note = (start, hz, length, decay, level, glide) => ({start, hz, length, decay, level, glide});
export const CHIMES = {
  // From chime.cpp, unchanged.
  move: {notes: [note(0.0, 1174.7, 0.045, 0.01, 1.0, 0.94)], peak: 0.1},
  select: {notes: [note(0.0, 659.3, 0.16, 0.04, 0.8, 1.0), note(0.055, 987.8, 0.22, 0.055, 1.0, 1.0)], peak: 0.2},
  back: {notes: [note(0.0, 880.0, 0.16, 0.04, 0.9, 1.0), note(0.055, 587.3, 0.22, 0.055, 1.0, 1.0)], peak: 0.19},
  error: {notes: [note(0.0, 392.0, 0.2, 0.07, 1.0, 0.99), note(0.11, 311.1, 0.3, 0.09, 1.0, 0.99)], peak: 0.22},
  notify: {
    notes: [note(0.0, 784.0, 0.42, 0.11, 0.9, 1.0), note(0.075, 987.8, 0.42, 0.11, 0.9, 1.0), note(0.15, 1174.7, 0.52, 0.14, 1.0, 1.0)],
    peak: 0.18,
  },
  // Not in the app (its success dialogs open silently and it has no QR
  // sound), so these use the same bell voice with new notes, in C major.
  done: {notes: [note(0.0, 1046.5, 0.3, 0.07, 0.85, 1.0), note(0.075, 1318.5, 0.45, 0.11, 1.0, 1.0)], peak: 0.19},
  installed: {
    notes: [
      note(0.0, 523.25, 0.3, 0.08, 0.75, 1.0),
      note(0.07, 659.25, 0.3, 0.08, 0.8, 1.0),
      note(0.14, 783.99, 0.36, 0.1, 0.9, 1.0),
      note(0.21, 1046.5, 0.65, 0.17, 1.0, 1.0),
    ],
    peak: 0.21,
  },
  qr: {notes: [note(0.0, 1567.98, 0.2, 0.05, 0.85, 1.0), note(0.07, 2093.0, 0.45, 0.12, 1.0, 1.0)], peak: 0.18},
};

// chime.cpp's voice(): a soft bell with a quick octave "shine", raised-cosine
// attack and release, and a phase that integrates the glide.
function chimeVoice(n, t) {
  if (t < 0 || t >= n.length) return 0;
  const Attack = 0.004;
  const Release = 0.025;
  let envelope = Math.exp(-t / n.decay);
  if (t < Attack) envelope *= 0.5 - 0.5 * Math.cos((Math.PI * t) / Attack);
  const tail = n.length - t;
  if (tail < Release) envelope *= 0.5 - 0.5 * Math.cos((Math.PI * tail) / Release);
  const rate = Math.log(n.glide) / n.length;
  const phase = Math.abs(rate) < 1e-9 ? TAU * n.hz * t : (TAU * n.hz * (Math.exp(rate * t) - 1)) / rate;
  const body = Math.sin(phase);
  const shine = 0.18 * Math.exp(-t / (n.decay * 0.35)) * Math.sin(2 * phase);
  return n.level * envelope * (body + shine);
}

// chime.cpp's render(), returning floats at full scale = 1.
export function renderChime(design, rate = SR) {
  let end = 0;
  for (const n of design.notes) end = Math.max(end, n.start + n.length);
  const count = Math.floor(end * rate) + 1;
  const mixed = new Float64Array(count);
  for (let i = 0; i < count; i++) {
    const t = i / rate;
    for (const n of design.notes) mixed[i] += chimeVoice(n, t - n.start);
  }
  const alpha = 1 - Math.exp((-TAU * 5000) / rate);
  let first = 0;
  let second = 0;
  let loudest = 0;
  for (let i = 0; i < count; i++) {
    first += alpha * (mixed[i] - first);
    second += alpha * (first - second);
    mixed[i] = second;
    loudest = Math.max(loudest, Math.abs(second));
  }
  const scale = loudest > 0 ? design.peak / loudest : 0;
  const out = new Float32Array(count);
  for (let i = 0; i < count; i++) out[i] = clamp(mixed[i] * scale, -1, 1);
  return out;
}

// --------------------------------------------------------- designed sfx

const at = (t) => Math.round(t * SR);

function whoosh(o, t, vel, pan, rng, rng2) {
  const dur = 0.62;
  const peakAt = 0.36;
  const s0 = at(t);
  const len = at(dur);
  const a = new SVF();
  const b = new SVF();
  const r = new SVF();
  const dir = pan < 0 ? -1 : 1;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    const u = tt / dur;
    if ((i & 15) === 0) {
      // Up to a bright peak, then down a little: the "pitch" of the air.
      const c = tt < peakAt ? 380 * Math.pow(2700 / 380, smooth(tt / peakAt)) : 2700 * Math.pow(1100 / 2700, (tt - peakAt) / (dur - peakAt));
      a.set(c, 1.4);
      b.set(c * 1.15, 1.4);
      r.set(c * 0.7, 7);
    }
    const n1 = rng();
    const n2 = rng2();
    a.tick(n1);
    b.tick(n2);
    r.tick(0.5 * (n1 + n2));
    const env = tt < peakAt ? Math.pow(smooth(tt / peakAt), 1.6) : Math.exp(-(tt - peakAt) / 0.07) * smooth((dur - tt) / 0.04);
    const p = clamp(pan + 0.4 * dir * (2 * u - 1), -1, 1);
    const mono = r.bp * 1.1;
    const y = vel * env;
    emit2(o, s0 + i, (a.bp * 1.3 + mono) * y * panL(p), (b.bp * 1.3 + mono) * y * panR(p));
  }
}

const POP_NOTES = [72, 74, 76, 79, 81, 84]; // C major pentatonic, so runs of pops make a tune

function pop(o, t, vel, pan, index) {
  const f = midiHz(POP_NOTES[index % POP_NOTES.length]);
  const s0 = at(t);
  const len = at(0.2);
  let p1 = 0;
  let p2 = 0;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    const f1 = f * (0.55 + 0.45 * smooth(tt / 0.03));
    p1 += (TAU * f1) / SR;
    const e1 = smooth(tt / 0.002) * Math.exp(-tt / 0.045);
    let y = Math.sin(p1) * e1;
    const t2 = tt - 0.038;
    if (t2 > 0) {
      p2 += (TAU * f * 1.5 * (0.7 + 0.3 * smooth(t2 / 0.02))) / SR;
      y += 0.45 * Math.sin(p2) * smooth(t2 / 0.002) * Math.exp(-t2 / 0.035);
    }
    y *= vel * smooth((len - i) / at(0.01));
    emit2(o, s0 + i, y * panL(pan), y * panR(pan));
  }
}

function typeKey(o, t, vel, pan, rng, index) {
  const s0 = at(t);
  const len = at(0.06);
  const hp = new SVF();
  hp.set(2600 + 400 * (index % 3), 0.9);
  const thock = 170 + 25 * ((index * 7) % 4);
  let ph = 0;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    ph += (TAU * thock) / SR;
    hp.tick(rng());
    const y = (hp.hp * Math.exp(-tt / 0.0035) * 0.9 + 0.5 * Math.sin(ph) * Math.exp(-tt / 0.012)) * smooth(tt / 0.0004) * smooth((len - i) / at(0.008)) * vel;
    emit2(o, s0 + i, y * panL(pan), y * panR(pan));
  }
}

// A soft plastic latch (two quick clicks) and a hinge swish, no creak.
function lid(o, t, vel, pan, rng, rng2) {
  const s0 = at(t);
  const len = at(0.75);
  const c = new SVF();
  c.set(3200, 1.5);
  const sl = new SVF();
  const sr = new SVF();
  let pb = 0;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    let click = 0;
    for (let j = 0; j < LATCH.length; j++) {
      const u = tt - LATCH[j][0];
      if (u >= 0 && u < 0.05) click += LATCH[j][1] * Math.exp(-u / 0.003) * smooth(u / 0.0004);
    }
    const n = rng();
    c.tick(n);
    pb += (TAU * 420) / SR;
    const body = Math.sin(pb) * Math.exp(-tt / 0.018) * 0.35;
    if ((i & 15) === 0) {
      const f = 500 + 1100 * smooth((tt - 0.05) / 0.35);
      sl.set(f, 0.9);
      sr.set(f * 1.1, 0.9);
    }
    sl.tick(n);
    sr.tick(rng2());
    const sw = smooth((tt - 0.04) / 0.25) * smooth((0.7 - tt) / 0.35) * 0.65;
    const y = (c.bp * click * 1.6 + body * smooth(tt / 0.001)) * vel;
    const g = smooth((len - i) / at(0.02)) * vel;
    emit2(o, s0 + i, (y + sl.bp * sw * g) * panL(pan), (y + sr.bp * sw * g) * panR(pan));
  }
}
const LATCH = [[0, 1], [0.028, 0.6]];

// Power-on shimmer: a warm low swell, then C-major partials blooming in turn
// with a fast flutter, and a breath of high noise.
function boot(o, t, vel, pan, rng, rng2) {
  const s0 = at(t);
  const dur = 1.2;
  const len = at(dur);
  const notes = [72, 79, 84, 86, 88, 91].map(midiHz);
  const ph = new Float64Array(notes.length);
  const hl = new SVF();
  const hr = new SVF();
  hl.set(6000, 0.7);
  hr.set(6500, 0.7);
  let pl = 0;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    pl += (TAU * (70 + 70 * smooth(tt / 0.25))) / SR;
    const low = Math.sin(pl) * smooth(tt / 0.03) * Math.exp(-tt / 0.18) * 0.5;
    let shimmer = 0;
    for (let k = 0; k < notes.length; k++) {
      ph[k] += (TAU * notes[k]) / SR;
      const u = tt - 0.05 - k * 0.045;
      if (u <= 0) continue;
      const flutter = 1 - 0.3 * (k > 2 ? 0.5 + 0.5 * Math.sin(TAU * 17 * tt + k) : 0);
      shimmer += Math.sin(ph[k]) * smooth(u / 0.12) * Math.exp(-u / 0.38) * flutter * (k < 2 ? 0.5 : 0.32);
    }
    hl.tick(rng());
    hr.tick(rng2());
    const air = smooth(tt / 0.2) * Math.exp(-tt / 0.3) * 0.22;
    const g = smooth((len - i) / at(0.08)) * vel;
    const m = (low + shimmer) * g;
    emit2(o, s0 + i, (m + hl.hp * air * g) * panL(pan), (m + hr.hp * air * g) * panR(pan));
  }
}

// Soft camera shutter: open click, a short mechanical rush, close click.
function shutter(o, t, vel, pan, rng) {
  const s0 = at(t);
  const len = at(0.2);
  const bp = new SVF();
  bp.set(2400, 1.2);
  const hp = new SVF();
  hp.set(5000, 0.7);
  let pt = 0;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    const n = rng();
    bp.tick(n);
    hp.tick(n);
    let click = 0;
    let thump = 0;
    for (let j = 0; j < SHUTTER.length; j++) {
      const u = tt - SHUTTER[j][0];
      const a = SHUTTER[j][1];
      if (u >= 0) {
        click += a * Math.exp(-u / 0.004) * smooth(u / 0.0004);
        thump += a * Math.exp(-u / 0.014) * smooth(u / 0.001);
      }
    }
    pt += (TAU * 125) / SR;
    const rush = tt > 0.012 && tt < 0.07 ? smooth((tt - 0.012) / 0.01) * smooth((0.07 - tt) / 0.015) * 0.18 : 0;
    const y = (bp.bp * click * 1.5 + Math.sin(pt) * thump * 0.4 + hp.hp * rush) * smooth((len - i) / at(0.01)) * vel;
    emit2(o, s0 + i, y * panL(pan), y * panR(pan));
  }
}
const SHUTTER = [[0, 1], [0.078, 0.75]];

// ------------------------------------------------------------- the cues

export const CUE_KINDS = [
  'tap', 'move', 'select', 'back', 'chime', 'done', 'installed', 'notify', 'error',
  'type', 'whoosh', 'impact', 'pop', 'riser', 'lid', 'boot', 'shutter',
];

// How long each sound lasts (seconds after its frame), for the composer.
export const CUE_LENGTH = {
  tap: 0.28, move: 0.05, select: 0.28, back: 0.28, chime: 0.52, done: 0.53, installed: 0.86, notify: 0.67,
  error: 0.41, type: 0.06, whoosh: 0.62, impact: 1.4, pop: 0.2, riser: 4.0, lid: 0.75, boot: 1.2, shutter: 0.2,
};

// The app's sounds are quiet on purpose (peaks 0.10 to 0.22); in the video
// they sit over music, so they are raised by this much (about +6 dB).
const UI = 2.0;
const UI_KIND = {tap: 'select', move: 'move', select: 'select', back: 'back', chime: 'qr', done: 'done', installed: 'installed', notify: 'notify', error: 'error'};

export function renderCues(cues, {fps, framesPerBar, out, rev}) {
  const cache = {};
  const counts = {};
  const sorted = [...cues].sort((a, b) => a.frame - b.frame);
  for (const cue of sorted) {
    if (!CUE_KINDS.includes(cue.kind)) throw new Error(`unknown cue kind ${cue.kind} at frame ${cue.frame}`);
    const t = cue.frame / fps;
    const gain = cue.gain ?? 1;
    const pan = clamp(cue.pan ?? 0, -1, 1);
    const k = (counts[cue.kind] = (counts[cue.kind] ?? -1) + 1);
    const seed = (cue.frame * 2654435761 + k * 97) >>> 0;
    const rng = makeRng(seed);
    const rng2 = makeRng(seed ^ 0x9e3779b9);
    const dry = {...out, ra: 0.06};
    const wet = (amt) => ({...out, ra: amt});
    const ui = UI_KIND[cue.kind];
    if (ui) {
      const pcm = (cache[ui] ??= renderChime(CHIMES[ui]));
      const s0 = Math.round(t * SR);
      const g = UI * gain * (cue.kind === 'tap' ? 0.9 : 1);
      for (let i = 0; i < pcm.length; i++) emit2(dry, s0 + i, pcm[i] * g * panL(pan), pcm[i] * g * panR(pan));
      continue;
    }
    switch (cue.kind) {
      case 'whoosh':
        whoosh(wet(0.12), t, 0.17 * gain, pan, rng, rng2);
        break;
      case 'impact':
        impact(wet(0.18), t, 0.33 * gain, 1);
        break;
      case 'pop':
        pop(wet(0.08), t, 0.42 * gain, pan, k);
        break;
      case 'riser':
        riser(wet(0.15), t, t + (2 * framesPerBar) / fps, 0.17 * gain);
        break;
      case 'lid':
        lid(wet(0.08), t, 0.36 * gain, pan, rng, rng2);
        break;
      case 'boot':
        boot(wet(0.2), t, 0.25 * gain, pan, rng, rng2);
        break;
      case 'shutter':
        shutter(wet(0.06), t, 0.5 * gain, pan, rng);
        break;
      case 'type':
        typeKey(dry, t, 0.2 * gain, pan, rng, k);
        break;
    }
  }
}
