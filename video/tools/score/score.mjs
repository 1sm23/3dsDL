// The arrangement: turns the timeline's scenes into a list of note and hit
// events. Nothing here knows bar numbers; every section is laid out from its
// scene's `music` role, its length and its neighbours.
//
// Key: C major (the banner jingle's key: G5 C6 E6 G6, then C7 E7 G7).
// Main progression, one chord per bar: Cadd9, G/B, Am9, Fmaj9 (I V vi IV).
// It runs on across neighbouring sections (groove into lift into lift...) and
// restarts on the drop, the peak, and after a build, break or card, so short
// scenes never turn it into a I-V seesaw.
// The hook is the jingle grown into four bars: the same rising 5-1-3 pickup
// (G C E as three 16ths) landing on G, an answer, and in bar four the
// jingle's 1-3-5 sparkles (C E G) over Fmaj9.
//
// Style (second cut): premium and restrained. Clean four-on-the-floor, a
// sustained sub, few fills (a reverse swell or an open hat rather than tom
// runs), crashes only where the energy steps up, arps in 8ths except at the
// peak, and the hook only where it means something.

import {SCENES, BPM, BEATS_PER_BAR} from './timeline.ts';

export const BAR = (60 / BPM) * BEATS_PER_BAR;
export const STEP = BAR / 16;
export const T = (bar, step = 0) => bar * BAR + step * STEP;
const SWING = 0.01; // seconds late for off-beat 16ths on hats

// Voicings are voice-led around middle C. bass is the bass note (slash
// chords included), alt a second in-key bass note (the chord's fifth).
export const CHORDS = {
  C: {bass: 36, alt: 43, v: [60, 64, 67, 74], sparkle: [84, 88, 91]}, // Cadd9
  GB: {bass: 35, alt: 38, v: [59, 62, 67, 74], sparkle: [83, 86, 91]}, // G/B
  Am: {bass: 33, alt: 40, v: [60, 64, 67, 71], sparkle: [84, 88, 91]}, // Am9 (no root up top)
  F: {bass: 41, alt: 36, v: [57, 60, 64, 67], sparkle: [84, 88, 91]}, // Fmaj9
  Em: {bass: 40, alt: 35, v: [62, 64, 67, 71], sparkle: [83, 88, 91]}, // Em7
  Dm: {bass: 38, alt: 33, v: [60, 64, 65, 69], sparkle: [81, 84, 88]}, // Dm9
  F7: {bass: 41, alt: 36, v: [60, 64, 65, 69], sparkle: [84, 88, 89]}, // Fmaj7
  Gs: {bass: 43, alt: 38, v: [60, 62, 67, 74], sparkle: [84, 86, 91]}, // Gsus4
  G: {bass: 43, alt: 38, v: [59, 62, 67, 74], sparkle: [83, 86, 91]}, // G
  Ga: {bass: 43, alt: 38, v: [62, 67, 69, 71, 74], sparkle: [83, 86, 91]}, // Gadd9 stab
};
const MAIN = ['C', 'GB', 'Am', 'F'];
const INTRO = ['F', 'Em', 'Dm', 'Gs'];
const BREAK = ['Am', 'F', 'Dm', 'Gs'];
const MAIN_ROLES = new Set(['drop', 'groove', 'lift', 'peak', 'end']);

// The hook, in 16th steps: [step, length, midi]. PICKUP is the jingle's
// rising G4 C5 E5, played before H1 and H3.
const HOOK = [
  [[0, 3, 79], [3, 3, 76], [6, 2, 79], [8, 4, 81], [12, 2, 79], [14, 2, 76]], // over C
  [[0, 6, 74], [6, 2, 76], [8, 4, 74]], // over G/B
  [[0, 3, 79], [3, 3, 76], [6, 2, 79], [8, 4, 84], [12, 2, 83], [14, 2, 81]], // over Am
  [[0, 4, 81], [4, 2, 79], [8, 2, 84], [10, 2, 88], [12, 2, 91]], // over F, the sparkles
];
const PICKUP = [[13, 1, 67], [14, 1, 72], [15, 1, 76]];

// Per role: kick, clap and bass level, so the grooves under the UI demos sit
// below the drop and the peak.
const DRIVE = {drop: 1, peak: 1, lift: 0.92, groove: 0.8, end: 0.88};
// Per role: how hard the parts dip under the kick.
const DUCK = {intro: 0, build: 0.6, drop: 0.95, card: 0.6, groove: 0.8, break: 0.5, lift: 0.9, peak: 1, end: 0.8};

export function layout() {
  const scenes = [...SCENES].sort((a, b) => a.bar - b.bar);
  const bars = [];
  scenes.forEach((s, k) => {
    for (let i = 0; i < s.bars; i++) {
      bars[s.bar + i] = {
        bar: s.bar + i,
        i,
        n: s.bars,
        role: s.music,
        scene: s.id,
        prev: scenes[k - 1]?.music ?? null,
        next: scenes[k + 1]?.music ?? null,
      };
    }
  });
  for (let b = 0; b < bars.length; b++) {
    if (!bars[b]) throw new Error(`timeline has a hole at bar ${b}`);
  }
  // A running bar count through the main sections, for chords and hook.
  let k = 0;
  for (const x of bars) {
    if (!MAIN_ROLES.has(x.role)) {
      x.k = -1;
      continue;
    }
    if (x.i === 0 && (x.role === 'drop' || x.role === 'peak' || !MAIN_ROLES.has(x.prev))) k = 0;
    x.k = k++;
  }
  // Chords.
  for (const x of bars) {
    const {i, n, role} = x;
    const prev = bars[x.bar - 1]?.chord;
    let c;
    if (role === 'intro') c = INTRO[i % 4];
    else if (role === 'build') c = i === n - 1 ? 'G' : i === n - 2 && n >= 3 ? 'Gs' : 'F7';
    else if (role === 'break') c = i === n - 1 ? 'Gs' : BREAK[i % 4];
    else if (role === 'card') c = prev === 'F' ? 'Ga' : 'F';
    else if (role === 'end') {
      const fin = finalIndex(n);
      if (i < fin - 2) c = MAIN[x.k % 4];
      else if (i === fin - 2) c = 'F';
      else if (i === fin - 1) c = 'G';
      else c = 'C';
    } else c = MAIN[x.k % 4];
    x.chord = c;
    x.duck = DUCK[role] ?? 0.8;
  }
  // Where the hook plays: index into HOOK, or -1. Always in phase with the
  // chords (H1 over C ... H4 over F).
  for (const x of bars) {
    const {i, n, role, k: kk} = x;
    let h = -1;
    if (role === 'drop' || role === 'peak') h = kk % 4;
    // A groove straight after the drop finishes the drop's phrase.
    else if (role === 'groove' && x.prev === 'drop' && kk < 4) h = kk % 4;
    // Lifts: every other four-bar phrase of the running count.
    else if (role === 'lift' && kk % 8 >= 4) h = kk % 4;
    else if (role === 'end') {
      const fin = finalIndex(n);
      if (i < fin - 2) h = kk % 4;
      else if (i === fin - 2) h = 3;
    }
    x.hook = h;
  }
  return bars;
}

// The bar of the end section that carries the final chord: two bars before
// the end, so the last ~1.5 s can ring out to silence.
export const finalIndex = (n) => Math.max(0, n - 2);

export function compose() {
  const bars = layout();
  const ev = [];
  const add = (type, o) => ev.push({type, ...o});
  const drumLP = []; // segments [t0, t1, hz0, hz1] for the drum bus low-pass (20 kHz elsewhere)
  const kicks = [];
  const kickAt = (t, vel = 1) => {
    add('kick', {t, vel});
    kicks.push(t);
  };
  const hatT = (bar, s) => T(bar, s) + (s % 2 === 1 ? SWING : 0);
  const pickupBars = new Set();
  let fadeStart = null;
  let fadeEnd = null;

  for (const x of bars) {
    const {bar, i, n, role, next, prev, chord} = x;
    const C = CHORDS[chord];
    const last = i === n - 1;
    const t0 = T(bar);
    const d = x.duck;
    const padNotes = [C.bass + 12, ...C.v];

    // ---------------------------------------------------------- intro
    if (role === 'intro') {
      const x0 = i / n;
      const x1 = (i + 1) / n;
      for (const m of padNotes) {
        add('pad', {t: t0, dur: BAR, midi: m, vel: 0.5, c0: 380 * Math.pow(4, x0), c1: 380 * Math.pow(4, x1), attack: i === 0 ? 1.6 : 0.35, release: 0.9, duck: 0});
      }
      if (i % 3 === 0) motifBells(add, bar, i + 1 < n, 1);
      if (last && next === 'build') add('rswell', {t: T(bar + 1), dur: 1.0, vel: 0.25, notes: [65, 69, 72, 76]});
      continue;
    }

    // ---------------------------------------------------------- build
    // Rises out of the previous scene (or out of silence when it opens the
    // film): pad swell, the motif as bells, a filtered kick that opens up,
    // a riser, then a half-beat gap holding only the hook's pickup.
    if (role === 'build') {
      const x0 = i / n;
      const x1 = (i + 1) / n;
      const fromSilence = prev === null;
      const gapAt = last ? 12 : 16;
      const kv = 0.5 + 0.4 * x1;
      // From silence, the kick waits for the second half of the first bar.
      const kickFrom = fromSilence && i === 0 ? 8 : 0;
      for (let s = kickFrom; s < gapAt; s += 4) kickAt(T(bar, s), kv);
      if (i === 0) drumLP.push([t0 + kickFrom * STEP, T(bar + n - 1, 12), 260, 15000]);
      for (let s = fromSilence && i === 0 ? 4 : 0; s < gapAt; s++) {
        if (!last && s % 4 !== 2) continue;
        if (last && s % 2 === 1) continue;
        add('hat', {t: hatT(bar, s), vel: (s % 4 === 2 ? 0.45 : 0.24) * (0.6 + 0.4 * x1), open: false, pan: s % 4 === 2 ? 0.15 : -0.15});
      }
      if (i > 0 || !fromSilence) {
        add('clap', {t: T(bar, 4), vel: 0.4});
        if (!last) add('clap', {t: T(bar, 12), vel: 0.4});
      }
      const swell = fromSilence && i === 0;
      for (const m of padNotes) {
        add('pad', {t: t0, dur: (gapAt / 16) * BAR - (last ? 0.05 : 0), midi: m, vel: 0.42, c0: 600 * Math.pow(6, x0), c1: 600 * Math.pow(6, x1), attack: swell ? 1.4 : 0.2, release: last ? 0.05 : 0.5, duck: 0.5 * d});
      }
      if (swell) motifBells(add, bar, n > 1, 0.85);
      if (i > 0 || n === 1) {
        const notes = last ? [[0, 2], [2, 2], [4, 2], [6, 2], [8, 2], [10, 2]] : [[0, 7], [8, 7]];
        notes.forEach(([s, l]) => add('bass', {t: T(bar, s), dur: l * STEP - 0.02, midi: C.bass, vel: 0.72, duck: 0.6 * d}));
        const ept = last ? [[0, 4], [4, 4], [8, 4]] : [[0, 8], [8, 8]];
        ept.forEach(([s, l]) => C.v.forEach((m, k) => add('ep', {t: T(bar, s), dur: l * STEP - 0.03, midi: m, vel: 0.35, bright: 0.45, pan: (k - 1.5) * 0.2, duck: 0.5 * d})));
      }
      if (last) {
        add('riser', {t0: T(bar - i), t1: T(bar, gapAt), vel: 0.32, tremolo: false});
        add('rswell', {t: T(bar + 1), dur: (16 - gapAt) * STEP, vel: 0.5});
      }
      continue;
    }

    // ------------------------------------------------------------ card
    if (role === 'card') {
      kickAt(t0, 1);
      add('impact', {t: t0, vel: 0.62, size: 0.9});
      add('crash', {t: t0, vel: 0.5, decay: 1.6});
      C.v.forEach((m, k) => {
        add('pluck', {t: t0, midi: m + 12, vel: 0.5, decay: 0.45, cut: 7000, pan: (k - 1.5) * 0.3, dly: 0.25, duck: 0});
        add('ep', {t: t0, dur: BAR * 0.75, midi: m, vel: 0.55, bright: 1, pan: (k - 1.5) * 0.25, duck: 0});
      });
      padNotes.forEach((m) => add('pad', {t: t0, dur: BAR * 0.7, midi: m, vel: 0.3, c0: 3500, c1: 1500, attack: 0.02, release: 0.5, duck: 0}));
      add('bass', {t: t0, dur: 6 * STEP, midi: C.bass, vel: 0.9, decay: 0.8, duck: 0});
      C.sparkle.forEach((m, k) => add('bell', {t: T(bar, 6 + 2 * k), midi: m, vel: 0.3 - 0.04 * k, decay: 0.45, pan: -0.3 + 0.3 * k}));
      [2, 6, 10].forEach((s) => add('hat', {t: hatT(bar, s), vel: 0.25, open: false, pan: 0.2}));
      [14, 15].forEach((s, k) => add('snare', {t: T(bar, s), vel: 0.3 + 0.15 * k, tune: 1 + 0.08 * k}));
      continue;
    }

    // ----------------------------------------------------------- break
    // Half time and filtered. The pulse plays one note per beat for the
    // first half (one connection), then four per beat (four connections).
    if (role === 'break') {
      const x1 = (i + 1) / n;
      const x0 = i / n;
      if (!last) {
        kickAt(T(bar, 0), 0.85);
        kickAt(T(bar, 10), 0.55);
        add('clap', {t: T(bar, 8), vel: 0.55, rev: 0.45});
      } else {
        [0, 4, 8, 12].forEach((s) => kickAt(T(bar, s), 0.85));
        [4, 12].forEach((s) => add('clap', {t: T(bar, s), vel: 0.5}));
        add('riser', {t0: t0, t1: T(bar + 1), vel: 0.24, from: 55, to: 84, tremolo: false});
      }
      if (i === 0) {
        add('impact', {t: t0, vel: 0.25, size: 0.8});
        drumLP.push([t0, T(bar + n - 1), 3000, 3000]);
        drumLP.push([T(bar + n - 1), T(bar + n), 3000, 20000]);
      }
      const fast = i >= Math.floor(n / 2);
      const every = fast ? 1 : 4;
      for (let s = 0; s < 16; s += every) {
        const accent = s % 4 === 0;
        add('hat', {t: hatT(bar, s), vel: (accent ? 0.34 : 0.2) * (last ? 0.6 + 0.4 * (s / 16) : 1), open: false, pan: accent ? 0.2 : -0.2});
        const m = C.v[C.v.length - 1 - ((s / every) % 2)] + 12;
        add('pluck', {t: T(bar, s), midi: m, vel: fast ? 0.2 : 0.28, decay: fast ? 0.08 : 0.16, cut: 900 + 2500 * x1, pan: accent ? -0.25 : 0.25, dly: 0.12, duck: 0.4 * d});
      }
      for (const m of padNotes) {
        add('pad', {t: t0, dur: BAR, midi: m, vel: 0.5, c0: last ? 700 : 520 + 200 * x0, c1: last ? 4200 : 520 + 200 * x1, attack: 0.3, release: 0.5, duck: 0.4 * d});
      }
      add('bass', {t: t0, dur: BAR - 0.05, midi: C.bass, vel: 0.8, decay: 2.5, duck: 0.4 * d});
      continue;
    }

    // -------------------------------------- drop / groove / lift / peak / end
    const fin = role === 'end' ? finalIndex(n) : -1;
    if (role === 'end' && i >= fin) {
      if (i === fin) {
        // The final chord, with the jingle landing on it.
        kickAt(t0, 0.7);
        add('crash', {t: t0, vel: 0.3, decay: 1.4});
        add('impact', {t: t0, vel: 0.22, size: 0.8});
        [48, ...C.v, 79].forEach((m, k) => add('ep', {t: t0, dur: 2.6, midi: m, vel: 0.5, bright: 0.8, pan: (k - 2.5) * 0.18, duck: 0}));
        padNotes.forEach((m) => add('pad', {t: t0, dur: 1.0, midi: m, vel: 0.36, c0: 3000, c1: 900, attack: 0.02, release: 1.4, duck: 0}));
        add('bass', {t: t0, dur: 2.2, midi: C.bass, vel: 0.85, decay: 0.55, duck: 0});
        add('jingle', {t: t0, vel: 0.85}); // t is where its held G6 lands
        const tEnd = T(bar + (n - i));
        fadeStart = tEnd - 2.6;
        fadeEnd = tEnd - 1.45;
      }
      continue;
    }

    const kk = x.k;
    const endGroove = role === 'end';
    const preFinal = role === 'end' && i === fin - 1;
    const light = role === 'groove' || endGroove;
    const v8 = kk % 8;
    const dropout = n >= 8 && i % 8 === 6 && (role === 'groove' || role === 'lift') && !last;
    const toBreak = last && next === 'break';
    const toPeak = last && next === 'peak';
    const toCard = last && next === 'card';
    // Step up into a bigger section: a short reverse swell, crash after.
    const rank = {groove: 1, end: 1, lift: 2, drop: 3, peak: 3};
    const stepUp = last && !toPeak && (rank[next] ?? 0) > (rank[role] ?? 0);
    const stopAt = preFinal || toPeak ? 12 : 16;
    const drive = DRIVE[role] ?? 0.9;
    const breath = !last && i % 4 === 3; // an open hat to mark four bars

    // Crashes only where the energy steps up.
    const entering = i === 0 && (rank[role] ?? 0) > (rank[prev] ?? 0);
    if (i === 0 && role === 'drop') add('impact', {t: t0, vel: 0.8, size: 1.1});
    if (i === 0 && role === 'peak') add('impact', {t: t0, vel: 0.5, size: 1});
    if (entering || (i === 0 && (prev === 'break' || prev === 'build' || prev === 'card'))) {
      add('crash', {t: t0, vel: role === 'drop' || role === 'peak' ? 0.6 : 0.4, decay: role === 'drop' ? 2.0 : 1.5});
    }

    // Kick.
    for (let s = 0; s < stopAt; s += 4) {
      if (dropout && s > 0) continue;
      if (toBreak && s === 12) continue;
      kickAt(T(bar, s), drive);
    }

    // Clap (and a quiet snare layer at the peak).
    const clapVel = (light ? 0.6 : 0.72) * drive;
    for (const s of [4, 12]) {
      if (s === 12 && (toBreak || toPeak || preFinal)) continue;
      add('clap', {t: T(bar, s), vel: clapVel});
      if (role === 'peak') add('snare', {t: T(bar, s), vel: 0.3, tune: 1.1});
    }

    // Hats: 8ths, open on the off-beats in the bigger sections, 16th ghosts
    // only in the second half of a phrase or at the peak.
    const hatEnd = toBreak || toPeak || preFinal || toCard ? 12 : 16;
    for (let s = 0; s < hatEnd; s++) {
      const off8 = s % 4 === 2;
      const ghost = s % 2 === 1 && (role === 'peak' || (light && v8 >= 4) || dropout);
      if (light) {
        if (off8) add('hat', {t: hatT(bar, s), vel: 0.4, open: breath && s === 14, pan: 0.18});
        else if (ghost) add('hat', {t: hatT(bar, s), vel: 0.12, open: false, pan: -0.25});
      } else {
        if (off8) add('hat', {t: hatT(bar, s), vel: role === 'lift' ? 0.28 : 0.32, open: true, pan: 0.12});
        else if (s % 4 === 0) add('hat', {t: hatT(bar, s), vel: 0.22, open: false, pan: -0.2});
        else if (ghost) add('hat', {t: hatT(bar, s), vel: 0.14, open: false, pan: -0.28});
      }
    }

    // Transitions: no tom runs, just air into the next downbeat.
    if (toBreak) add('rswell', {t: T(bar + 1), dur: 4 * STEP, vel: 0.3, notes: [69, 72, 76, 81]});
    if (stepUp || toCard) add('rswell', {t: T(bar + 1), dur: 3 * STEP, vel: 0.28});
    if (toPeak) {
      add('riser', {t0: T(Math.max(bar - 1, bar - i)), t1: T(bar, 12), vel: 0.28, tremolo: false});
      add('rswell', {t: T(bar + 1), dur: 4 * STEP, vel: 0.45});
    }
    if (preFinal) add('rswell', {t: T(bar + 1), dur: 4 * STEP, vel: 0.3});

    // Bass: a sustained, pumping sub in the big sections; house off-beats
    // under the demos.
    const B = (k) => (k === 'o' ? C.bass + 12 : k === 'a' ? C.alt : C.bass);
    let bp;
    if (role === 'drop' || role === 'peak') bp = [[0, 6, 'r'], [6, 2, 'o'], [8, 6, 'r'], [14, 2, 'a']];
    else if (role === 'lift') bp = [[0, 3, 'r'], [3, 3, 'r'], [6, 2, 'o'], [8, 3, 'r'], [11, 3, 'r'], [14, 2, 'a']];
    else if (dropout) bp = [[0, 6, 'r'], [8, 6, 'o']];
    else if (preFinal) bp = [[0, 2, 'r'], [2, 2, 'r'], [4, 2, 'r'], [6, 2, 'r'], [8, 2, 'a'], [10, 2, 'a']];
    else if (v8 >= 4) bp = [[0, 1, 'r'], [2, 2, 'r'], [6, 2, 'r'], [10, 2, 'r'], [12, 1, 'a'], [14, 2, 'r']];
    else bp = [[2, 2, 'r'], [6, 2, 'r'], [10, 2, 'r'], [14, 2, 'o']];
    for (const [s, l, k] of bp) {
      if (s >= stopAt) continue;
      add('bass', {t: T(bar, s), dur: Math.min(l, stopAt - s) * STEP - 0.015, midi: B(k), vel: 0.95 * drive * (dropout ? 0.75 : 1), decay: dropout ? 0.5 : 0, duck: 0.65 * d});
    }

    // Electric piano: a 3-3-2 figure, or off-beat stabs late in a groove.
    let pat;
    if (preFinal) pat = [[0, 6], [6, 6]];
    else if (role === 'groove' && v8 >= 4) pat = [[2, 2], [6, 2], [10, 3], [13, 3]];
    else pat = [[0, 5], [6, 5], [12, 4]];
    const bright = light ? 0.45 : role === 'lift' ? 0.6 : 0.72;
    const epVel = role === 'groove' ? 0.58 : role === 'peak' || role === 'drop' ? 0.6 : 0.5;
    for (const [s, l] of pat) {
      if (s >= stopAt) continue;
      C.v.forEach((m, k) =>
        add('ep', {t: T(bar, s), dur: Math.min(l, stopAt - s) * STEP - 0.03, midi: m, vel: epVel, bright, pan: (k - 1.5) * 0.22, duck: 0.55 * d}),
      );
    }

    // Pad (not under the UI demos).
    if (role !== 'groove') {
      const pv = role === 'peak' ? 0.48 : role === 'drop' ? 0.4 : role === 'lift' ? 0.3 : 0.22;
      const pc = role === 'peak' ? 4000 : role === 'drop' ? 3200 : 2300;
      for (const m of padNotes) {
        add('pad', {t: t0, dur: (stopAt / 16) * BAR - (stopAt < 16 ? 0.04 : 0), midi: m, vel: pv, c0: pc, c1: pc, attack: 0.08, release: stopAt < 16 ? 0.05 : 0.45, duck: 0.8 * d});
      }
    }

    // Arpeggio: 8ths, 16ths only at the peak.
    if (role === 'drop' || role === 'lift' || role === 'peak' || (endGroove && !preFinal && i < fin - 2)) {
      const tones = [...C.v.map((m) => m + 12), C.v[0] + 24].sort((a, b) => a - b);
      const p = Math.floor(kk / 4) % 2 ? [0, 2, 1, 3, 2, 4, 3, 1] : [0, 1, 2, 3, 1, 2, 3, 4];
      const every = role === 'peak' ? 1 : 2;
      const av = role === 'peak' ? 0.27 : role === 'lift' ? 0.3 : 0.26;
      for (let s = 0, j = 0; s < stopAt; s += every, j++) {
        add('pluck', {t: T(bar, s), midi: tones[p[j % 8]], vel: av * (s % 4 === 0 ? 1 : 0.75), decay: every === 1 ? 0.13 : 0.2, cut: role === 'peak' ? 3800 : 3000, pan: j % 2 ? 0.35 : -0.35, dly: 0.3, duck: 0.35 * d});
      }
    }

    // Groove: a light pluck figure, and the jingle's pickup as an answer
    // before the phrase turns over, low enough to leave the UI sounds room.
    if (role === 'groove') {
      const nb = bars[bar + 1];
      const motif = kk % 4 === 3 && !last && x.hook < 0 && nb?.hook < 0;
      for (const [s, k] of [[3, 0], [6, 1], [11, 0], [14, 1]]) {
        if (motif && s === 14) continue;
        const m = C.v[C.v.length - 1 - k];
        add('pluck', {t: T(bar, s), midi: m, vel: 0.26, decay: 0.12, cut: 1600, pan: k ? 0.3 : -0.3, dly: 0.22, duck: 0.4 * d});
      }
      if (motif) {
        PICKUP.forEach(([s, , m]) => add('pluck', {t: T(bar, s), midi: m, vel: 0.24, decay: 0.15, cut: 2000, pan: 0, dly: 0.25, duck: 0}));
        add('pluck', {t: T(bar + 1, 0), midi: 79, vel: 0.24, decay: 0.3, cut: 2200, pan: 0, dly: 0.3, duck: 0});
      }
    }

    // The outro's held melody before the final chord.
    if (preFinal) {
      [[0, 6, 74], [6, 2, 76], [8, 3, 74]].forEach(([s, l, m]) => add('lead', {t: T(bar, s), dur: l * STEP * 0.92, midi: m, vel: 0.36, pan: 0}));
    }
  }

  // The hook.
  const leadVel = {drop: 0.5, groove: 0.34, lift: 0.4, peak: 0.52, end: 0.42};
  const pickup = (bar, vel) => {
    if (pickupBars.has(bar)) return;
    pickupBars.add(bar);
    PICKUP.forEach(([s, l, m]) => add('lead', {t: T(bar, s), dur: l * STEP * 0.85, midi: m, vel, pan: 0}));
  };
  for (const x of bars) {
    const h = x.hook;
    if (h < 0) continue;
    const vel = leadVel[x.role] ?? 0.36;
    for (const [s, l, m] of HOOK[h]) {
      add('lead', {t: T(x.bar, s), dur: l * STEP * 0.9, midi: m, vel, pan: 0});
      if (x.role === 'peak') {
        add('bell', {t: T(x.bar, s), midi: m + 12, vel: 0.12, decay: 0.35, pan: 0.2});
        add('lead', {t: T(x.bar, s), dur: l * STEP * 0.9, midi: m - 12, vel: vel * 0.35, pan: -0.15});
      }
    }
    if (h === 3) {
      HOOK[3].slice(2).forEach(([s, , m], k) => add('bell', {t: T(x.bar, s), midi: m, vel: 0.18, decay: 0.5, pan: -0.3 + 0.3 * k}));
    }
    const nh = bars[x.bar + 1]?.hook ?? -1;
    if ((h === 1 || h === 3) && (nh === 0 || nh === 2)) pickup(x.bar, vel * 0.9);
    const ph = bars[x.bar - 1]?.hook ?? -1;
    if ((h === 0 || h === 2) && ph < 0 && x.bar > 0) pickup(x.bar - 1, vel * 0.9);
  }

  drumLP.sort((a, b) => a[0] - b[0]);
  const total = T(bars.length);
  return {events: ev, kicks: kicks.sort((a, b) => a - b), drumLP, bars, total, fadeStart, fadeEnd};
}

// The motif as slow bells: G5 C6 E6 on beats 3, 3.5 and 4, then G6 on the
// next downbeat (when there is one).
function motifBells(add, bar, landing, vel) {
  add('bell', {t: T(bar, 8), midi: 79, vel: 0.32 * vel, decay: 0.7, pan: -0.25});
  add('bell', {t: T(bar, 10), midi: 84, vel: 0.3 * vel, decay: 0.7, pan: 0.05});
  add('bell', {t: T(bar, 12), midi: 88, vel: 0.28 * vel, decay: 0.8, pan: 0.25});
  if (landing) add('bell', {t: T(bar + 1, 0), midi: 91, vel: 0.3 * vel, decay: 1.4, pan: 0});
}
