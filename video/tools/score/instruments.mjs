// The soundtrack's voices. Each one adds a single note or hit into an output
// (a stereo bus plus optional reverb and delay sends and a sidechain duck
// curve), starting at a time in seconds. All oscillators are band-limited
// (PolyBLEP or additive with partials kept under Nyquist) and every
// envelope starts and ends at zero.

import {SR, TAU, midiHz, makeRng, SVF, blep, smooth, clamp} from './dsp.mjs';

// Where a voice writes. duck is the kick's sidechain curve (0..1 per
// sample) and depth how far this part dips under it.
export function makeOut(bus, {duck = null, depth = 0, rev = null, revAmt = 0, dly = null, dlyAmt = 0} = {}) {
  return {L: bus.L, R: bus.R, N: bus.L.length, duck, depth, rv: rev, ra: revAmt, dl: dly, da: dlyAmt};
}

export function emit2(o, n, yl, yr) {
  if (n < 0 || n >= o.N) return;
  if (o.duck !== null && o.depth > 0) {
    const g = 1 - o.depth * o.duck[n];
    yl *= g;
    yr *= g;
  }
  o.L[n] += yl;
  o.R[n] += yr;
  if (o.rv !== null) {
    o.rv.L[n] += yl * o.ra;
    o.rv.R[n] += yr * o.ra;
  }
  if (o.dl !== null) {
    o.dl.L[n] += yl * o.da;
    o.dl.R[n] += yr * o.da;
  }
}

// Linear balance: centre is unity in both channels.
export const panL = (p) => Math.min(1, 1 - p);
export const panR = (p) => Math.min(1, 1 + p);

const at = (t) => Math.round(t * SR);
let seed = 1;
const freshRng = () => makeRng((seed = (seed * 1103515245 + 12345) >>> 0) || 7);

// ---------------------------------------------------------------- drums

export function kick(o, t, vel = 1) {
  const s0 = at(t);
  const len = at(0.42);
  const rng = freshRng();
  let ph = 0;
  const sat = Math.tanh(1.7);
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    const f = 47 + 125 * Math.exp(-tt / 0.028) + 60 * Math.exp(-tt / 0.004);
    ph += (TAU * f) / SR;
    const amp = smooth(tt / 0.0012) * Math.exp(-tt / 0.2) * smooth((len - i) / at(0.03));
    let v = Math.tanh(1.7 * Math.sin(ph) * amp) / sat;
    v += 0.18 * rng() * Math.exp(-tt / 0.0015) * smooth(tt / 0.0004);
    emit2(o, s0 + i, v * vel, v * vel);
  }
}

export function clap(o, t, vel = 1, pan = 0) {
  const s0 = at(t);
  const len = at(0.42);
  const rng = freshRng();
  const rng2 = freshRng();
  const fl = new SVF();
  const fr = new SVF();
  fl.set(1250, 1.1);
  fr.set(1350, 1.1);
  const hl = new SVF();
  hl.set(700, 0.7);
  const hr = new SVF();
  hr.set(700, 0.7);
  const BK = [0, 0.0105, 0.021];
  const BA = [0.75, 0.85, 1];
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    let env = 0;
    for (let b = 0; b < 3; b++) {
      const k = BK[b];
      if (tt >= k) env += BA[b] * Math.exp(-(tt - k) / 0.0045) * smooth((tt - k) / 0.0006);
    }
    if (tt >= 0.021) env += 0.55 * Math.exp(-(tt - 0.021) / 0.12);
    env *= smooth((len - i) / at(0.03));
    fl.tick(rng());
    fr.tick(rng2());
    hl.tick(fl.bp);
    hr.tick(fr.bp);
    const y = 2.2 * env * vel;
    emit2(o, s0 + i, hl.hp * y * panL(pan), hr.hp * y * panR(pan));
  }
}

const HAT_HZ = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0].map((f) => f * 2.1);

export function hat(o, t, vel = 1, open = false, pan = 0) {
  const s0 = at(t);
  const decay = open ? 0.24 : 0.032;
  const len = at(open ? 0.7 : 0.13);
  const rng = freshRng();
  const ph = new Float64Array(6);
  const hp = new SVF();
  hp.set(7200, 0.8);
  const bp = new SVF();
  bp.set(10500, 1.2);
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    let metal = 0;
    for (let k = 0; k < 6; k++) {
      const dt = HAT_HZ[k] / SR;
      let p = ph[k] + dt;
      if (p >= 1) p -= 1;
      ph[k] = p;
      metal += (p < 0.5 ? 1 : -1) + blep(p, dt) - blep(p < 0.5 ? p + 0.5 : p - 0.5, dt);
    }
    const x = 0.55 * rng() + 0.07 * metal;
    hp.tick(x);
    bp.tick(hp.hp);
    const env = smooth(tt / 0.0006) * Math.exp(-tt / decay) * smooth((len - i) / at(0.02));
    const y = (hp.hp * 0.8 + bp.bp * 0.6) * env * vel;
    emit2(o, s0 + i, y * panL(pan), y * panR(pan));
  }
}

export function snare(o, t, vel = 1, tune = 1, pan = 0) {
  const s0 = at(t);
  const len = at(0.3);
  const rng = freshRng();
  const bp = new SVF();
  bp.set(3800 * tune, 0.9);
  const hp = new SVF();
  hp.set(1400, 0.7);
  let ph = 0;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    const f = 190 * tune * (1 + 0.5 * Math.exp(-tt / 0.012));
    ph += (TAU * f) / SR;
    const body = Math.sin(ph) * Math.exp(-tt / 0.055);
    bp.tick(rng());
    hp.tick(bp.bp);
    const noise = hp.hp * 2.0 * Math.exp(-tt / 0.11);
    const env = smooth(tt / 0.0008) * smooth((len - i) / at(0.03));
    const y = (0.55 * body + noise) * env * vel;
    emit2(o, s0 + i, y * panL(pan), y * panR(pan));
  }
}

export function tom(o, t, vel = 1, f0 = 140, pan = 0) {
  const s0 = at(t);
  const len = at(0.45);
  const rng = freshRng();
  let ph = 0;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    const f = f0 * (1 + 0.55 * Math.exp(-tt / 0.03));
    ph += (TAU * f) / SR;
    const env = smooth(tt / 0.001) * Math.exp(-tt / 0.17) * smooth((len - i) / at(0.04));
    const y = (Math.sin(ph) + 0.25 * Math.sin(2 * ph) * Math.exp(-tt / 0.05) + 0.05 * rng() * Math.exp(-tt / 0.01)) * env * vel;
    emit2(o, s0 + i, y * panL(pan), y * panR(pan));
  }
}

const CRASH_HZ = [301.3, 455.2, 610.7, 802.9, 1011.4, 1223.6].map((f) => f * 3.2);

export function crash(o, t, vel = 1, decay = 1.5) {
  const s0 = at(t);
  const len = at(Math.min(5, decay * 3.6));
  const rl = freshRng();
  const rr = freshRng();
  const ph = new Float64Array(6);
  const hl = new SVF();
  const hr = new SVF();
  hl.set(4800, 0.7);
  hr.set(5200, 0.7);
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    let metal = 0;
    for (let k = 0; k < 6; k++) {
      const dt = CRASH_HZ[k] / SR;
      let p = ph[k] + dt;
      if (p >= 1) p -= 1;
      ph[k] = p;
      metal += (p < 0.5 ? 1 : -1) + blep(p, dt) - blep(p < 0.5 ? p + 0.5 : p - 0.5, dt);
    }
    hl.tick(rl() + 0.06 * metal);
    hr.tick(rr() + 0.06 * metal);
    const env =
      smooth(tt / 0.001) * (0.65 * Math.exp(-tt / decay) + 0.35 * Math.exp(-tt / 0.08)) * smooth((len - i) / at(0.3));
    emit2(o, s0 + i, hl.hp * env * vel, hr.hp * env * vel);
  }
}

// A short wooden tick for a clock-like pulse.
export function tick(o, t, vel = 1, f = 1800, pan = 0) {
  const s0 = at(t);
  const len = at(0.05);
  const rng = freshRng();
  const bp = new SVF();
  bp.set(f * 1.5, 3);
  let ph = 0;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    ph += (TAU * f) / SR;
    bp.tick(rng());
    const env = smooth(tt / 0.0005) * Math.exp(-tt / 0.009) * smooth((len - i) / at(0.01));
    const y = (0.7 * Math.sin(ph) + 0.6 * bp.bp) * env * vel;
    emit2(o, s0 + i, y * panL(pan), y * panR(pan));
  }
}

// ----------------------------------------------------------------- tonal

// Round sub bass: a sine with a little second harmonic and a soft low-passed
// saw for definition on small speakers. Mono.
export function bass(o, t, dur, midi, vel = 1, decay = 0) {
  const s0 = at(t);
  const f = midiHz(midi);
  const rel = 0.03;
  const len = at(dur + rel);
  const lp = new SVF();
  lp.set(Math.min(420, f * 5), 0.8);
  let ph = 0;
  let sp = 0;
  const dt = f / SR;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    ph += TAU * dt;
    sp += dt;
    if (sp >= 1) sp -= 1;
    const saw = 2 * sp - 1 - blep(sp, dt);
    lp.tick(saw);
    const env =
      smooth(tt / 0.005) * smooth((dur + rel - tt) / rel) * (decay > 0 ? Math.exp(-tt / decay) : 1);
    const y = (Math.sin(ph) + 0.18 * Math.sin(2 * ph) + 0.22 * lp.lp * (0.6 + 0.4 * Math.exp(-tt / 0.12))) * env * vel;
    emit2(o, s0 + i, y, y);
  }
}

// FM electric piano: a 1:1 modulator whose index falls after the strike,
// a quick tine partial, and a slow auto-pan. bright scales the bark.
export function ep(o, t, dur, midi, vel = 1, bright = 0.7, pan = 0) {
  const s0 = at(t);
  const f = midiHz(midi);
  const rel = 0.09;
  const len = at(dur + rel);
  let pc = 0;
  let pm = 0;
  let pt = 0;
  const tineHz = f * 7;
  const tineOk = tineHz < 16000;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    pc += (TAU * f) / SR;
    pm += (TAU * f * 1.0007) / SR;
    pt += (TAU * tineHz) / SR;
    const index = bright * (1.9 * Math.exp(-tt / 0.16) + 0.35) * (0.6 + 0.4 * vel);
    let y = Math.sin(pc + index * Math.sin(pm));
    if (tineOk) y += 0.12 * bright * Math.exp(-tt / 0.035) * Math.sin(pt);
    const env = smooth(tt / 0.003) * (0.55 * Math.exp(-tt / 0.5) + 0.45 * Math.exp(-tt / 2.5)) * smooth((dur + rel - tt) / rel);
    y *= env * vel;
    const p = clamp(pan + 0.18 * Math.sin(TAU * 3.2 * (t + tt)), -1, 1);
    emit2(o, s0 + i, y * panL(p), y * panR(p));
  }
}

// Soft saw pad: three detuned PolyBLEP saws spread left, centre and right,
// through a low-pass that moves from c0 to c1 over the note.
export function pad(o, t, dur, midi, vel = 1, c0 = 1500, c1 = 1500, attack = 0.3, release = 0.6) {
  const s0 = at(t);
  const f = midiHz(midi);
  const len = at(dur + release);
  const det = [Math.pow(2, -0.1 / 12), 1, Math.pow(2, 0.1 / 12)];
  const ph = [0.11, 0.53, 0.82];
  const fl = new SVF();
  const fr = new SVF();
  const v = new Float64Array(3);
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    if ((i & 15) === 0) {
      const x = clamp(tt / Math.max(0.01, dur), 0, 1);
      const fc = c0 * Math.pow(c1 / c0, x);
      fl.set(fc, 0.75);
      fr.set(fc * 1.04, 0.75);
    }
    for (let k = 0; k < 3; k++) {
      const dt = (f * det[k]) / SR;
      let p = ph[k] + dt;
      if (p >= 1) p -= 1;
      ph[k] = p;
      v[k] = 2 * p - 1 - blep(p, dt);
    }
    const l = fl.tick(v[0] + 0.7 * v[1]);
    const r = fr.tick(v[2] + 0.7 * v[1]);
    const env = smooth(tt / attack) * smooth((dur + release - tt) / release);
    const g = env * vel * 0.35;
    emit2(o, s0 + i, l * g, r * g);
  }
}

// Pluck: saw plus square through a low-pass with a fast closing envelope.
export function pluck(o, t, midi, vel = 1, {decay = 0.2, cut = 4000, pan = 0, q = 1.1} = {}) {
  const s0 = at(t);
  const f = midiHz(midi);
  const len = at(Math.min(1.6, decay * 5 + 0.02));
  const dt = f / SR;
  const lp = new SVF();
  let p1 = 0;
  let p2 = 0.25;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    if ((i & 7) === 0) lp.set(Math.min(16000, f * 1.2 + cut * Math.exp(-tt / 0.05)), q);
    p1 += dt;
    if (p1 >= 1) p1 -= 1;
    p2 += dt;
    if (p2 >= 1) p2 -= 1;
    const saw = 2 * p1 - 1 - blep(p1, dt);
    const sq = (p2 < 0.5 ? 1 : -1) + blep(p2, dt) - blep(p2 < 0.5 ? p2 + 0.5 : p2 - 0.5, dt);
    lp.tick(0.6 * saw + 0.4 * sq);
    const env = smooth(tt / 0.0015) * Math.exp(-tt / decay) * smooth((len - i) / at(0.012));
    const y = lp.lp * env * vel;
    emit2(o, s0 + i, y * panL(pan), y * panR(pan));
  }
}

// The hook voice: an additive triangle (like the banner jingle's notes) with
// a glassy bell partial on the strike and a gentle delayed vibrato. The odd
// harmonics come from a Chebyshev recurrence, one sin/cos per sample.
export function lead(o, t, dur, midi, vel = 1, pan = 0) {
  const s0 = at(t);
  const f = midiHz(midi);
  const rel = 0.1;
  const len = at(dur + rel);
  let maxK = 1;
  while ((maxK + 2) * f < 15000 && maxK < 15) maxK += 2;
  const bellOk = f * 2.76 < 18000;
  let ph = 0;
  let pb = 0;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    const vib = 1 + 0.0028 * smooth((tt - 0.18) / 0.25) * Math.sin(TAU * 5.3 * tt);
    ph += (TAU * f * vib) / SR;
    pb += (TAU * f * 2.76) / SR;
    // Triangle: sum over odd k of (-1)^((k-1)/2) sin(k ph) / k^2.
    const s1 = Math.sin(ph);
    const c2 = 2 * Math.cos(2 * ph);
    let prev = -s1; // sin(-ph)
    let cur = s1;
    let tri = s1;
    for (let k = 3, sign = -1; k <= maxK; k += 2, sign = -sign) {
      const nxt = c2 * cur - prev;
      prev = cur;
      cur = nxt;
      tri += (sign * cur) / (k * k);
    }
    tri *= 0.81;
    let y = tri + 0.18 * Math.sin(2 * ph) * Math.exp(-tt / 0.12);
    if (bellOk) y += 0.22 * Math.sin(pb) * Math.exp(-tt / 0.22);
    const env = smooth(tt / 0.004) * (0.62 + 0.38 * Math.exp(-tt / 0.22)) * smooth((dur + rel - tt) / rel);
    y *= env * vel;
    emit2(o, s0 + i, y * panL(pan), y * panR(pan));
  }
}

// Jingle-style bell: a sine and an inharmonic partial at 2.76x.
export function bell(o, t, midi, vel = 1, decay = 0.5, pan = 0, hz = 0) {
  const s0 = at(t);
  const f = hz || midiHz(midi);
  const len = at(Math.min(4, decay * 6));
  const pOk = f * 2.76 < 18000;
  const p2Ok = f * 5.4 < 18000;
  let ph = 0;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    ph += (TAU * f) / SR;
    let y = Math.sin(ph);
    if (pOk) y += 0.3 * Math.sin(2.76 * ph) * Math.exp(-tt / (decay * 0.6));
    if (p2Ok) y += 0.08 * Math.sin(5.4 * ph) * Math.exp(-tt / (decay * 0.2));
    const env = smooth(tt / 0.003) * Math.exp(-tt / decay) * smooth((len - i) / at(0.05));
    y *= env * vel;
    emit2(o, s0 + i, y * panL(pan), y * panR(pan));
  }
}

// The jingle's landing "bop": a sine dropping from 560 to 250 Hz.
export function bop(o, t, vel = 1, pan = 0) {
  const s0 = at(t);
  const dur = 0.16;
  const len = at(dur);
  let ph = 0;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    const f = 560 * Math.pow(250 / 560, i / len);
    ph += (TAU * f) / SR;
    const env = smooth(tt / 0.004) * Math.exp(-14 * tt) * smooth((dur - tt) / 0.01);
    const y = Math.sin(ph) * env * vel;
    emit2(o, s0 + i, y * panL(pan), y * panR(pan));
  }
}

// --------------------------------------------------------------- effects

// Impact: a falling sub, a noise burst and a low tonal hit, stereo.
export function impact(o, t, vel = 1, size = 1) {
  const s0 = at(t);
  const len = at(1.6 * size);
  const rl = freshRng();
  const rr = freshRng();
  const fl = new SVF();
  const fr = new SVF();
  let ps = 0;
  let pt = 0;
  const partials = [1, 2, 3.01, 4.23];
  const weights = [1, 0.45, 0.25, 0.12];
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    const fs = 34 + 80 * Math.exp(-tt / 0.11);
    ps += (TAU * fs) / SR;
    const sub = Math.sin(ps) * Math.exp(-tt / (0.42 * size));
    pt += (TAU * 130.81) / SR;
    let tone = 0;
    for (let k = 0; k < 4; k++) tone += weights[k] * Math.sin(partials[k] * pt) * Math.exp(-tt / (0.32 / (k + 1)));
    if ((i & 15) === 0) {
      const fc = 400 + 5200 * Math.exp(-tt / 0.07);
      fl.set(fc, 0.7);
      fr.set(fc * 1.07, 0.7);
    }
    fl.tick(rl());
    fr.tick(rr());
    const nEnv = Math.exp(-tt / 0.1) * 0.9 + 0.12 * Math.exp(-tt / 0.6);
    const env = smooth(tt / 0.0008) * smooth((len - i) / at(0.1));
    const mid = (1.0 * sub + 0.32 * tone) * env;
    emit2(o, s0 + i, (mid + fl.lp * nEnv * env) * vel, (mid + fr.lp * nEnv * env) * vel);
  }
}

// Riser: band-passed noise and a saw gliding up together, swelling to t1,
// then gone within 40 ms.
export function riser(o, t0, t1, vel = 1, {from = 46, to = 84, tremolo = true} = {}) {
  const s0 = at(t0);
  const dur = t1 - t0;
  const len = at(dur + 0.04);
  const rl = freshRng();
  const rr = freshRng();
  const bl = new SVF();
  const br = new SVF();
  const tl = new SVF();
  let ps = 0;
  let ps2 = 0.37;
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    const x = Math.min(1, tt / dur);
    if ((i & 15) === 0) {
      const fc = 280 * Math.pow(9500 / 280, x);
      bl.set(fc, 1.6);
      br.set(fc * 1.12, 1.6);
      tl.set(Math.min(15000, midiHz(from + (to - from) * x) * 3), 0.9);
    }
    const f = midiHz(from + (to - from) * x * x * 0.4 + (to - from) * x * 0.6);
    const dt = f / SR;
    ps += dt;
    if (ps >= 1) ps -= 1;
    ps2 += dt * 1.006;
    if (ps2 >= 1) ps2 -= 1;
    const saw = 2 * ps - 1 - blep(ps, dt) + 2 * ps2 - 1 - blep(ps2, dt * 1.006);
    tl.tick(saw * 0.5);
    bl.tick(rl());
    br.tick(rr());
    const trem = tremolo ? 1 - 0.35 * x * (0.5 + 0.5 * Math.sin(TAU * (4 + 20 * x * x) * tt)) : 1;
    const env = Math.pow(x, 2.2) * smooth((dur + 0.04 - tt) / 0.04) * trem;
    const tone = tl.lp * 0.35;
    emit2(o, s0 + i, (bl.bp * 1.4 + tone) * env * vel, (br.bp * 1.4 + tone) * env * vel);
  }
}

// Reverse swell ending exactly at tEnd: a decaying noise wash and a C chord
// of bells, rendered forward and written backwards.
export function reverseSwell(o, tEnd, dur = 0.5, vel = 1, notes = [72, 76, 79, 84]) {
  const len = at(dur);
  const L = new Float32Array(len);
  const R = new Float32Array(len);
  const rl = freshRng();
  const rr = freshRng();
  const fl = new SVF();
  const fr = new SVF();
  fl.set(5000, 0.7);
  fr.set(5600, 0.7);
  const phs = new Float64Array(notes.length);
  const hz = notes.map(midiHz);
  for (let i = 0; i < len; i++) {
    const tt = i / SR;
    fl.tick(rl());
    fr.tick(rr());
    const e = Math.exp(-tt / (dur * 0.3));
    let tone = 0;
    for (let k = 0; k < notes.length; k++) {
      phs[k] += (TAU * hz[k]) / SR;
      tone += Math.sin(phs[k]) * (k % 2 ? 0.8 : 1);
    }
    tone *= 0.12;
    L[i] = (fl.lp * 0.7 + tone) * e;
    R[i] = (fr.lp * 0.7 + tone) * e;
  }
  const s1 = at(tEnd);
  for (let i = 0; i < len; i++) {
    const n = s1 - 1 - i; // reversed: sample i of the forward render lands i before tEnd
    const g = smooth(i / at(0.004)) * smooth((len - i) / at(0.02)) * vel;
    emit2(o, n, L[i] * g, R[i] * g);
  }
}

// The banner jingle from resources/banner/jingle.py, note for note: a quick
// rising arpeggio, a held G6, the falling "bop", three sparkles. The python
// triangle (asin(sin)) is swapped for an additive one so nothing aliases.
export function jingle(o, t, vel = 1) {
  const tone = (start, dur, freq, volume, kind = 'triangle', endFreq = 0, decay = 6) => {
    const s0 = at(t + start);
    const count = at(dur);
    let ph = 0;
    for (let i = 0; i < count; i++) {
      const tt = i / SR;
      const f = endFreq ? freq * Math.pow(endFreq / freq, i / count) : freq;
      ph += (TAU * f) / SR;
      let w;
      if (kind === 'triangle') {
        w = 0;
        for (let k = 1, sign = 1; k * f < 15000 && k <= 15; k += 2, sign = -sign) w += (sign * Math.sin(k * ph)) / (k * k);
        w *= 8 / (Math.PI * Math.PI);
      } else if (kind === 'bell') {
        w = Math.sin(ph) + (f * 2.76 < 18000 ? 0.3 * Math.sin(ph * 2.76) : 0);
      } else {
        w = Math.sin(ph);
      }
      const attack = smooth(tt / 0.004);
      const release = smooth((count - i) / (0.01 * SR));
      const y = volume * attack * release * Math.exp(-decay * tt) * w * vel;
      emit2(o, s0 + i, y, y);
    }
  };
  [784.0, 1046.5, 1318.5].forEach((f, k) => tone(0.06 + k * 0.085, 0.11, f, 0.32, 'triangle', 0, 10));
  tone(0.315, 0.3, 1568.0, 0.3, 'triangle', 0, 7);
  tone(0.7, 0.16, 560.0, 0.5, 'sine', 250.0, 14);
  [2093.0, 2637.0, 3136.0].forEach((f, k) => tone(0.95 + k * 0.1, 0.5, f, 0.16 - k * 0.03, 'bell', 0, 7));
}
// Seconds from the jingle's start to its held G6, so callers can land it on a beat.
export const JINGLE_LAND = 0.315;
