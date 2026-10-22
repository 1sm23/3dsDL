// Builds the launch video's soundtrack from code: no samples, no libraries.
//
//   node audio/build.mjs                 (from video/)
//   node audio/build.mjs --out DIR --cues extra.json [--cues more.json] [--no-screen-cues] [--stems] [--quiet]
//
// Writes music.wav, sfx.wav and soundtrack.wav (music + sfx) to
// public/audio/ (or --out), 48 kHz stereo 16-bit, exactly as long as the
// timeline, and prints peak, RMS and loudness per scene.
//
// Cues come from src/cues.ts, src/screen-cues.generated.json when it exists,
// and any --cues files (JSON arrays of {frame, kind, gain?, pan?}).

import {existsSync, mkdirSync, readFileSync} from 'node:fs';
import {dirname, join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {
  SR, SVF, stereo, freeverb, pingPong, compress, limit, biquad, filterInPlace,
  kHops, integrated, maxMomentary, stats, writeWav, dbToGain, smooth,
} from './dsp.mjs';
import * as I from './instruments.mjs';

// Node warns that the timeline's .ts has no module type; that is expected.
process.removeAllListeners('warning');
const {FPS, FRAMES_PER_BAR, TOTAL_FRAMES, SCENES} = await import('./timeline.ts');
const {CUES} = await import('./cues.ts');
const {compose, STEP} = await import('./score.mjs');
const {renderCues} = await import('./sfx.mjs');

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');

const args = process.argv.slice(2);
const opt = (name) => {
  const out = [];
  for (let k = 0; k < args.length; k++) if (args[k] === name && args[k + 1]) out.push(args[++k]);
  return out;
};
const outDir = resolve(opt('--out')[0] ?? join(root, 'assets/music'));
const quiet = args.includes('--quiet');
const stems = args.includes('--stems');
const log = (...a) => quiet || console.log(...a);

const TARGET_LUFS = -16;
const CEILING = -1;

const started = Date.now();
const N = Math.round((TOTAL_FRAMES / FPS) * SR);
const {events, kicks, drumLP, total, fadeStart, fadeEnd} = compose();
if (Math.abs(total - N / SR) > 1e-6) throw new Error(`score is ${total}s, timeline ${N / SR}s`);

// ------------------------------------------------------------ sidechain

const duck = new Float32Array(N);
for (const tk of kicks) {
  const s0 = Math.round(tk * SR);
  const len = Math.round(0.26 * SR);
  for (let i = 0; i < len && s0 + i < N; i++) {
    const tt = i / SR;
    const d = smooth(tt / 0.002) * (tt < 0.018 ? 1 : 1 - smooth((tt - 0.018) / 0.22));
    if (d > duck[s0 + i]) duck[s0 + i] = d;
  }
}

// ---------------------------------------------------------------- render

const drums = stereo(N);
const bassBus = stereo(N);
const inst = stereo(N);
const fx = stereo(N);
const revSend = stereo(N);
const dlySend = stereo(N);

const out = (bus, ev, defaults = {}) =>
  I.makeOut(bus, {
    duck: ev.duck ? duck : null,
    depth: ev.duck ?? 0,
    rev: revSend,
    revAmt: ev.rev ?? defaults.rev ?? 0,
    dly: (ev.dly ?? defaults.dly) ? dlySend : null,
    dlyAmt: ev.dly ?? defaults.dly ?? 0,
  });

const render = {
  kick: (e) => I.kick(out(drums, e), e.t, e.vel),
  clap: (e) => I.clap(out(drums, e, {rev: 0.2}), e.t, e.vel * 0.55, e.pan ?? 0),
  hat: (e) => I.hat(out(drums, e, {rev: 0.03}), e.t, e.vel * 0.42, e.open, e.pan ?? 0),
  snare: (e) => I.snare(out(drums, e, {rev: 0.16}), e.t, e.vel * 0.5, e.tune ?? 1, e.pan ?? 0),
  tom: (e) => I.tom(out(drums, e, {rev: 0.14}), e.t, e.vel * 0.55, e.f, e.pan ?? 0),
  crash: (e) => I.crash(out(drums, e, {rev: 0.12}), e.t, e.vel * 0.22, e.decay),
  tick: (e) => I.tick(out(drums, e, {rev: 0.18}), e.t, e.vel * 0.5, e.f, e.pan ?? 0),
  bass: (e) => I.bass(out(bassBus, e), e.t, e.dur, e.midi, e.vel * 0.5, e.decay ?? 0),
  ep: (e) => I.ep(out(inst, e, {rev: 0.16}), e.t, e.dur, e.midi, e.vel * 0.19, e.bright, e.pan ?? 0),
  pad: (e) => I.pad(out(inst, e, {rev: 0.32}), e.t, e.dur, e.midi, e.vel * 0.22, e.c0, e.c1, e.attack, e.release),
  pluck: (e) => I.pluck(out(inst, e, {rev: 0.12}), e.t, e.midi, e.vel * 0.3, {decay: e.decay, cut: e.cut, pan: e.pan ?? 0}),
  lead: (e) => I.lead(out(inst, e, {rev: 0.2, dly: 0.16}), e.t, e.dur, e.midi, e.vel * 0.5, e.pan ?? 0),
  bell: (e) => I.bell(out(inst, e, {rev: 0.38, dly: 0.14}), e.t, e.midi, e.vel * 0.5, e.decay, e.pan ?? 0),
  bop: (e) => I.bop(out(inst, e, {rev: 0.15}), e.t, e.vel * 0.5, e.pan ?? 0),
  jingle: (e) => I.jingle(out(inst, e, {rev: 0.3, dly: 0.12}), e.t - I.JINGLE_LAND, e.vel * 0.55),
  impact: (e) => I.impact(out(fx, e, {rev: 0.22}), e.t, e.vel * 0.55, e.size ?? 1),
  riser: (e) => I.riser(out(fx, e, {rev: 0.2}), e.t0, e.t1, e.vel * 0.4, {from: e.from, to: e.to, tremolo: e.tremolo ?? true}),
  rswell: (e) => I.reverseSwell(out(fx, e, {rev: 0.05}), e.t, e.dur, e.vel * 0.45, e.notes),
};
for (const e of events) {
  const r = render[e.type];
  if (!r) throw new Error(`no voice for ${e.type}`);
  r(e);
}
log(`rendered ${events.length} events in ${((Date.now() - started) / 1000).toFixed(1)} s`);

// Drum bus low-pass automation (the build's opening filter, the break).
{
  const fl = new SVF();
  const fr = new SVF();
  let seg = 0;
  for (let n = 0; n < N; n += 32) {
    const t = n / SR;
    while (seg < drumLP.length && t >= drumLP[seg][1]) seg++;
    let fc = 20000;
    const s = drumLP[seg];
    if (s && t >= s[0]) fc = s[2] * Math.pow(s[3] / s[2], (t - s[0]) / (s[1] - s[0]));
    fl.set(fc, 0.75);
    fr.set(fc, 0.75);
    for (let i = n; i < Math.min(N, n + 32); i++) {
      drums.L[i] = fl.tick(drums.L[i]);
      drums.R[i] = fr.tick(drums.R[i]);
    }
  }
}

// Sends: ping-pong delay (3/16) feeding the reverb a little, then the room.
const dlyRet = stereo(N);
pingPong(dlySend, dlyRet, {time: 3 * STEP, feedback: 0.42, damp: 4200, hp: 350, gain: 1});
for (let n = 0; n < N; n++) {
  revSend.L[n] += dlyRet.L[n] * 0.2;
  revSend.R[n] += dlyRet.R[n] * 0.2;
}
const revRet = stereo(N);
freeverb(revSend, revRet, {room: 0.87, damp: 0.4, gain: 1, predelay: 0.02});
// Keep the reverb's low end out of the mix.
for (const ch of ['L', 'R']) filterInPlace(revRet[ch], biquad('hp', 220, 0.6));
log(`effects done in ${((Date.now() - started) / 1000).toFixed(1)} s`);

// ---------------------------------------------------------------- music mix

if (stems) {
  mkdirSync(join(outDir, 'stems'), {recursive: true});
  const st = {drums, bass: bassBus, inst, fx, reverb: revRet, delay: dlyRet};
  for (const [k, b] of Object.entries(st)) {
    const c = copy(b);
    scale(c, 0.25);
    writeWav(join(outDir, 'stems', `${k}.wav`), c);
  }
}

const music = stereo(N);
for (let n = 0; n < N; n++) {
  const pump = 1 - 0.3 * duck[n];
  music.L[n] = drums.L[n] + bassBus.L[n] + inst.L[n] + fx.L[n] + (revRet.L[n] + dlyRet.L[n] * 0.9) * pump;
  music.R[n] = drums.R[n] + bassBus.R[n] + inst.R[n] + fx.R[n] + (revRet.R[n] + dlyRet.R[n] * 0.9) * pump;
}

function master(buf, {fade = true} = {}) {
  // DC and rumble out; everything below 120 Hz in mono.
  for (const ch of ['L', 'R']) {
    filterInPlace(buf[ch], biquad('hp', 24, 0.707));
  }
  const side = new Float32Array(N);
  for (let n = 0; n < N; n++) side[n] = (buf.L[n] - buf.R[n]) * 0.5;
  filterInPlace(side, biquad('hp', 120, 0.707));
  filterInPlace(side, biquad('hp', 120, 0.707));
  for (let n = 0; n < N; n++) {
    const mid = (buf.L[n] + buf.R[n]) * 0.5;
    buf.L[n] = mid + side[n];
    buf.R[n] = mid - side[n];
  }
  if (fade && fadeStart !== null) {
    const a = Math.round(fadeStart * SR);
    const b = Math.round(fadeEnd * SR);
    for (let n = a; n < N; n++) {
      const g = n >= b ? 0 : 1 - smooth((n - a) / (b - a));
      buf.L[n] *= g;
      buf.R[n] *= g;
    }
  }
}

function scale(buf, g) {
  for (let n = 0; n < N; n++) {
    buf.L[n] *= g;
    buf.R[n] *= g;
  }
}
function copy(buf) {
  return {L: Float32Array.from(buf.L), R: Float32Array.from(buf.R)};
}
const lufs = (buf) => integrated(kHops(buf));

master(music);
// Glue: bring it to a known level, compress gently, then find the gain that
// lands on the loudness target after the limiter.
scale(music, dbToGain(-19 - lufs(music)));
const glueGr = compress(music, {threshold: -15, ratio: 2, knee: 8, attack: 0.015, release: 0.18});
let gain = dbToGain(TARGET_LUFS - lufs(music));
let musicOut;
let musicLufs;
let limited = 0;
for (let pass = 0; pass < 4; pass++) {
  musicOut = copy(music);
  scale(musicOut, gain);
  limited = limit(musicOut, {ceilingDb: CEILING});
  musicLufs = lufs(musicOut);
  if (Math.abs(musicLufs - TARGET_LUFS) < 0.05) break;
  gain *= dbToGain(TARGET_LUFS - musicLufs);
}

// -------------------------------------------------------------------- sfx

const cueFiles = [...(args.includes('--no-screen-cues') ? [] : [join(here, 'screen-cues.json')]), ...opt('--cues').map((p) => resolve(p))];
const cues = [...CUES];
for (const f of cueFiles) {
  if (!existsSync(f)) continue;
  const list = JSON.parse(readFileSync(f, 'utf8'));
  if (!Array.isArray(list)) throw new Error(`${f} is not an array`);
  cues.push(...list);
  log(`cues: ${list.length} from ${f}`);
}
const sfx = stereo(N);
const sfxRevSend = stereo(N);
const outside = cues.filter((c) => !(c.frame >= 0 && c.frame < TOTAL_FRAMES));
if (outside.length) {
  console.warn(`warning: ${outside.length} cue(s) fall outside the timeline (0..${TOTAL_FRAMES - 1}) and were skipped; is a cue file stale?`);
}
renderCues(
  cues.filter((c) => c.frame >= 0 && c.frame < TOTAL_FRAMES),
  {fps: FPS, framesPerBar: FRAMES_PER_BAR, out: I.makeOut(sfx, {rev: sfxRevSend, revAmt: 0}), rev: sfxRevSend},
);
const sfxRev = stereo(N);
freeverb(sfxRevSend, sfxRev, {room: 0.72, damp: 0.5, gain: 1, predelay: 0.008});
const sfxDry = copy(sfx);
for (let n = 0; n < N; n++) {
  sfx.L[n] += sfxRev.L[n];
  sfx.R[n] += sfxRev.R[n];
}
master(sfx, {fade: false});
const sfxLimited = limit(sfx, {ceilingDb: CEILING});

// ------------------------------------------------------------- soundtrack

// The music dips up to 3 dB under the effects so they read clearly.
const soundtrack = stereo(N);
{
  const atk = 1 - Math.exp(-1 / (0.005 * SR));
  const rel = 1 - Math.exp(-1 / (0.22 * SR));
  let env = 0;
  for (let n = 0; n < N; n++) {
    const a = Math.max(Math.abs(sfxDry.L[n]), Math.abs(sfxDry.R[n]));
    env += (a > env ? atk : rel) * (a - env);
    const g = 1 - 0.29 * Math.min(1, env / 0.2);
    soundtrack.L[n] = musicOut.L[n] * g + sfx.L[n];
    soundtrack.R[n] = musicOut.R[n] * g + sfx.R[n];
  }
}
const stLimited = limit(soundtrack, {ceilingDb: CEILING});

// ------------------------------------------------------------- write

mkdirSync(outDir, {recursive: true});
for (const [name, buf] of [['music', musicOut], ['sfx', sfx], ['soundtrack', soundtrack]]) {
  const s = stats(buf);
  if (s.nonFinite) throw new Error(`${name} has ${s.nonFinite} non-finite samples`);
  const clipped = writeWav(join(outDir, `${name}.wav`), buf);
  if (clipped) throw new Error(`${name} clipped ${clipped} samples`);
}

// -------------------------------------------------------------- report

const hops = {music: kHops(musicOut), sfx: kHops(sfx), soundtrack: kHops(soundtrack)};
const f1 = (x) => (Number.isFinite(x) ? x.toFixed(1).padStart(6) : '  -inf');
const rows = [];
for (const s of [...SCENES].sort((a, b) => a.bar - b.bar)) {
  const t0 = (s.bar * FRAMES_PER_BAR) / FPS;
  const t1 = ((s.bar + s.bars) * FRAMES_PER_BAR) / FPS;
  const a = Math.round(t0 * SR);
  const b = Math.round(t1 * SR);
  const h0 = Math.round(t0 * 10);
  const h1 = Math.round(t1 * 10);
  const m = stats(musicOut, a, b);
  const st = stats(soundtrack, a, b);
  rows.push(
    `${s.id.padEnd(9)} ${s.music.padEnd(6)} ${t0.toFixed(0).padStart(4)}-${t1.toFixed(0).padEnd(4)}` +
      ` | music pk ${f1(m.peakDb)} rms ${f1(m.rmsDb)} lufs ${f1(integrated(hops.music, h0, h1))} maxM ${f1(maxMomentary(hops.music, h0, h1))}` +
      ` | mix pk ${f1(st.peakDb)} lufs ${f1(integrated(hops.soundtrack, h0, h1))}`,
  );
}
const ms = stats(musicOut);
const ss = stats(sfx);
const ts = stats(soundtrack);
log('');
log(`scene     role    secs      | music (peak dBFS, RMS dBFS, integrated LUFS, max momentary) | soundtrack`);
rows.forEach((r) => log(r));
log('');
log(`music      ${f1(integrated(hops.music))} LUFS  peak ${f1(ms.peakDb)} dBFS  rms ${f1(ms.rmsDb)}  glue GR max ${glueGr.toFixed(1)} dB  limiter active ${(limited * 100).toFixed(1)}% of samples`);
log(`sfx        ${f1(integrated(hops.sfx))} LUFS  peak ${f1(ss.peakDb)} dBFS  (${cues.length} cues, limiter ${(sfxLimited * 100).toFixed(2)}%)`);
log(`soundtrack ${f1(integrated(hops.soundtrack))} LUFS  peak ${f1(ts.peakDb)} dBFS  (limiter ${(stLimited * 100).toFixed(2)}%)`);
log(`length ${(N / SR).toFixed(3)} s, ${N} samples; wrote ${outDir}; ${((Date.now() - started) / 1000).toFixed(1)} s`);
