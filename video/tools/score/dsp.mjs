// Small DSP toolkit for the soundtrack: oscillators, filters, reverb, delay,
// dynamics, loudness and WAV output. Everything works on Float32Arrays at
// 48 kHz and allocates per note or per pass, never per sample.

import {writeFileSync, readFileSync} from 'node:fs';

export const SR = 48000;
export const TAU = Math.PI * 2;

export const midiHz = (m) => 440 * Math.pow(2, (m - 69) / 12);
export const dbToGain = (db) => Math.pow(10, db / 20);
export const gainToDb = (g) => (g > 0 ? 20 * Math.log10(g) : -Infinity);
export const clamp = (x, lo, hi) => (x < lo ? lo : x > hi ? hi : x);

// Deterministic white noise in [-1, 1).
export function makeRng(seed = 1) {
  let s = seed >>> 0 || 1;
  return () => {
    s ^= s << 13;
    s >>>= 0;
    s ^= s >>> 17;
    s ^= s << 5;
    s >>>= 0;
    return s / 2147483648 - 1;
  };
}

// PolyBLEP residual for band-limited saw and square waves.
export function blep(t, dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1;
  }
  if (t > 1 - dt) {
    t = (t - 1) / dt;
    return t * t + t + t + 1;
  }
  return 0;
}

// Raised-cosine edge, 0 at x <= 0 and 1 at x >= 1, so envelopes never click.
export const smooth = (x) => (x <= 0 ? 0 : x >= 1 ? 1 : 0.5 - 0.5 * Math.cos(Math.PI * x));

// Topology-preserving state variable filter (Simper). Call set() whenever
// the cutoff moves (cheap enough every few samples), then tick().
export class SVF {
  constructor() {
    this.ic1 = 0;
    this.ic2 = 0;
    this.lp = 0;
    this.bp = 0;
    this.hp = 0;
    this.set(1000, 0.707);
  }
  set(fc, q) {
    const g = Math.tan((Math.PI * clamp(fc, 10, SR * 0.47)) / SR);
    this.k = 1 / q;
    this.a1 = 1 / (1 + g * (g + this.k));
    this.a2 = g * this.a1;
    this.a3 = g * this.a2;
  }
  tick(v0) {
    const v3 = v0 - this.ic2;
    const v1 = this.a1 * this.ic1 + this.a2 * v3;
    const v2 = this.ic2 + this.a2 * this.ic1 + this.a3 * v3;
    this.ic1 = 2 * v1 - this.ic1;
    this.ic2 = 2 * v2 - this.ic2;
    if (Math.abs(this.ic1) < 1e-20) this.ic1 = 0;
    if (Math.abs(this.ic2) < 1e-20) this.ic2 = 0;
    this.lp = v2;
    this.bp = v1;
    this.hp = v0 - this.k * v1 - v2;
    return v2;
  }
}

// RBJ biquad coefficients.
export function biquad(type, fc, q = 0.707, gainDb = 0) {
  const w = (TAU * fc) / SR;
  const cw = Math.cos(w);
  const sw = Math.sin(w);
  const alpha = sw / (2 * q);
  const A = Math.pow(10, gainDb / 40);
  let b0, b1, b2, a0, a1, a2;
  switch (type) {
    case 'lp':
      b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
      break;
    case 'hp':
      b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
      break;
    case 'peak':
      b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
      break;
    case 'lowshelf': {
      const s = 2 * Math.sqrt(A) * alpha;
      b0 = A * (A + 1 - (A - 1) * cw + s); b1 = 2 * A * (A - 1 - (A + 1) * cw); b2 = A * (A + 1 - (A - 1) * cw - s);
      a0 = A + 1 + (A - 1) * cw + s; a1 = -2 * (A - 1 + (A + 1) * cw); a2 = A + 1 + (A - 1) * cw - s;
      break;
    }
    case 'highshelf': {
      const s = 2 * Math.sqrt(A) * alpha;
      b0 = A * (A + 1 + (A - 1) * cw + s); b1 = -2 * A * (A - 1 + (A + 1) * cw); b2 = A * (A + 1 + (A - 1) * cw - s);
      a0 = A + 1 - (A - 1) * cw + s; a1 = 2 * (A - 1 - (A + 1) * cw); a2 = A + 1 - (A - 1) * cw - s;
      break;
    }
    default:
      throw new Error(`biquad ${type}`);
  }
  return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];
}

// Filters a buffer in place (transposed direct form II).
export function filterInPlace(x, c, from = 0, to = x.length) {
  const [b0, b1, b2, a1, a2] = c;
  let z1 = 0;
  let z2 = 0;
  for (let n = from; n < to; n++) {
    const v = x[n];
    const y = b0 * v + z1;
    z1 = b1 * v - a1 * y + z2;
    z2 = b2 * v - a2 * y;
    if (Math.abs(z1) < 1e-25) z1 = 0;
    if (Math.abs(z2) < 1e-25) z2 = 0;
    x[n] = y;
  }
}

export const stereo = (N) => ({L: new Float32Array(N), R: new Float32Array(N)});

// Freeverb (Jezar's tunings, rescaled to 48 kHz), with a short pre-delay.
export function freeverb(src, dst, {room = 0.86, damp = 0.35, gain = 1, predelay = 0.018, spread = 23} = {}) {
  const N = src.L.length;
  const scale = SR / 44100;
  const combT = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617];
  const apT = [556, 441, 341, 225];
  const pd = Math.round(predelay * SR);
  for (const [ch, off] of [['L', 0], ['R', spread]]) {
    const combs = combT.map((t) => new Float32Array(Math.round((t + off) * scale)));
    const store = new Float64Array(8);
    const ci = new Int32Array(8);
    const aps = apT.map((t) => new Float32Array(Math.round((t + off) * scale)));
    const ai = new Int32Array(4);
    const out = dst[ch];
    const inL = src.L;
    const inR = src.R;
    const d1 = damp;
    const d2 = 1 - damp;
    for (let n = 0; n < N; n++) {
      const m = n - pd;
      const input = m >= 0 ? (inL[m] + inR[m]) * 0.015 : 0;
      let acc = 0;
      for (let k = 0; k < 8; k++) {
        const buf = combs[k];
        const i = ci[k];
        const y = buf[i];
        let s = y * d2 + store[k] * d1;
        if (s < 1e-20 && s > -1e-20) s = 0;
        store[k] = s;
        buf[i] = input + s * room;
        ci[k] = i + 1 === buf.length ? 0 : i + 1;
        acc += y;
      }
      for (let k = 0; k < 4; k++) {
        const buf = aps[k];
        const i = ai[k];
        const b = buf[i];
        buf[i] = acc + b * 0.5;
        ai[k] = i + 1 === buf.length ? 0 : i + 1;
        acc = b - acc;
      }
      out[n] += acc * gain * 3;
    }
  }
}

// Tempo-synced ping-pong delay: echoes alternate L, R, L... each one darker.
export function pingPong(src, dst, {time = 0.375, feedback = 0.42, damp = 3500, hp = 300, gain = 1} = {}) {
  const N = src.L.length;
  const D = Math.round(time * SR);
  const bl = new Float32Array(D);
  const br = new Float32Array(D);
  const lpa = 1 - Math.exp((-TAU * damp) / SR);
  const hpa = 1 - Math.exp((-TAU * hp) / SR);
  let lpL = 0, lpR = 0, hpL = 0, hpR = 0;
  let w = 0;
  for (let n = 0; n < N; n++) {
    const dl = bl[w];
    const dr = br[w];
    const input = (src.L[n] + src.R[n]) * 0.5;
    lpR += lpa * (dr - lpR);
    hpR += hpa * (lpR - hpR);
    lpL += lpa * (dl - lpL);
    hpL += hpa * (lpL - hpL);
    let a = input + feedback * (lpR - hpR);
    let b = feedback * (lpL - hpL);
    if (Math.abs(a) < 1e-20) a = 0;
    if (Math.abs(b) < 1e-20) b = 0;
    bl[w] = a;
    br[w] = b;
    w = w + 1 === D ? 0 : w + 1;
    dst.L[n] += dl * gain;
    dst.R[n] += dr * gain;
  }
}

// Stereo-linked feed-forward compressor with a soft knee.
export function compress(buf, {threshold = -14, ratio = 2, knee = 6, attack = 0.012, release = 0.16, makeup = 0} = {}) {
  const {L, R} = buf;
  const N = L.length;
  const at = Math.exp(-1 / (attack * SR));
  const rt = Math.exp(-1 / (release * SR));
  const mk = dbToGain(makeup);
  let env = 0; // detector in dB of gain reduction
  let maxGr = 0;
  for (let n = 0; n < N; n++) {
    const a = Math.max(Math.abs(L[n]), Math.abs(R[n]));
    const x = a > 1e-6 ? 20 * Math.log10(a) : -120;
    const over = x - threshold;
    let gr = 0;
    if (2 * over > knee) gr = over - over / ratio;
    else if (2 * over > -knee) gr = ((1 - 1 / ratio) * (over + knee / 2) ** 2) / (2 * knee);
    env = gr > env ? at * env + (1 - at) * gr : rt * env + (1 - rt) * gr;
    if (env > maxGr) maxGr = env;
    const g = dbToGain(-env) * mk;
    L[n] *= g;
    R[n] *= g;
  }
  return maxGr;
}

// Look-ahead peak limiter: the gain is held over the look-ahead window, eased
// back up with the release, then averaged over the window so it is already
// down when a peak arrives. Ends with a hard ceiling that should never act.
export function limit(buf, {ceilingDb = -1, lookahead = 0.005, release = 0.09} = {}) {
  const {L, R} = buf;
  const N = L.length;
  const ceil = dbToGain(ceilingDb);
  const la = Math.round(lookahead * SR);
  const req = new Float32Array(N);
  for (let n = 0; n < N; n++) {
    const a = Math.max(Math.abs(L[n]), Math.abs(R[n]));
    req[n] = a > ceil ? ceil / a : 1;
  }
  // Sliding minimum over [n, n + la] with a monotonic deque.
  const held = new Float32Array(N);
  const dq = new Int32Array(N + la + 1);
  let head = 0, tail = 0;
  let next = 0;
  for (let n = 0; n < N; n++) {
    const end = Math.min(N - 1, n + la);
    while (next <= end) {
      while (tail > head && req[dq[tail - 1]] >= req[next]) tail--;
      dq[tail++] = next++;
    }
    while (dq[head] < n) head++;
    held[n] = req[dq[head]];
  }
  const rc = 1 - Math.exp(-1 / (release * SR));
  let g = 1;
  for (let n = 0; n < N; n++) {
    g = Math.min(held[n], g + (1 - g) * rc);
    held[n] = g;
  }
  let sum = 0;
  let reduced = 0;
  for (let n = 0; n < N; n++) {
    sum += held[n];
    if (n > la) sum -= held[n - la - 1];
    const gain = sum / Math.min(n + 1, la + 1);
    if (gain < 0.999) reduced++;
    let l = L[n] * gain;
    let r = R[n] * gain;
    if (l > ceil) l = ceil; else if (l < -ceil) l = -ceil;
    if (r > ceil) r = ceil; else if (r < -ceil) r = -ceil;
    L[n] = l;
    R[n] = r;
  }
  return reduced / N;
}

// ITU-R BS.1770 K-weighting (48 kHz coefficients) and gated loudness.
const K1 = [1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585];
const K2 = [1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621];

// Mean square of the K-weighted signal in 100 ms hops.
export function kHops(buf) {
  const N = buf.L.length;
  const hop = SR / 10;
  const hops = Math.floor(N / hop);
  const out = new Float64Array(hops);
  for (const ch of ['L', 'R']) {
    const x = Float32Array.from(buf[ch]);
    filterInPlace(x, K1);
    filterInPlace(x, K2);
    for (let h = 0; h < hops; h++) {
      let s = 0;
      for (let n = h * hop; n < (h + 1) * hop; n++) s += x[n] * x[n];
      out[h] += s / hop;
    }
  }
  return out;
}

// Integrated loudness (LUFS) over hops [from, to), with the absolute and
// relative gates on 400 ms blocks overlapping by 75 %.
export function integrated(hops, from = 0, to = hops.length) {
  const blocks = [];
  for (let h = from; h + 4 <= to; h++) {
    const ms = (hops[h] + hops[h + 1] + hops[h + 2] + hops[h + 3]) / 4;
    blocks.push(ms);
  }
  const lk = (ms) => -0.691 + 10 * Math.log10(ms + 1e-30);
  const abs = blocks.filter((ms) => lk(ms) > -70);
  if (!abs.length) return -Infinity;
  const rel = lk(abs.reduce((a, b) => a + b, 0) / abs.length) - 10;
  const gated = abs.filter((ms) => lk(ms) > rel);
  if (!gated.length) return -Infinity;
  return lk(gated.reduce((a, b) => a + b, 0) / gated.length);
}

// Highest momentary (400 ms) loudness over hops [from, to).
export function maxMomentary(hops, from = 0, to = hops.length) {
  let best = -Infinity;
  for (let h = from; h + 4 <= to; h++) {
    const ms = (hops[h] + hops[h + 1] + hops[h + 2] + hops[h + 3]) / 4;
    best = Math.max(best, -0.691 + 10 * Math.log10(ms + 1e-30));
  }
  return best;
}

export function stats(buf, from = 0, to = buf.L.length) {
  let peak = 0, ss = 0, dcL = 0, dcR = 0, bad = 0;
  for (let n = from; n < to; n++) {
    const l = buf.L[n];
    const r = buf.R[n];
    if (!Number.isFinite(l) || !Number.isFinite(r)) bad++;
    const a = Math.max(Math.abs(l), Math.abs(r));
    if (a > peak) peak = a;
    ss += l * l + r * r;
    dcL += l;
    dcR += r;
  }
  const count = Math.max(1, to - from);
  return {
    peakDb: gainToDb(peak),
    rmsDb: 10 * Math.log10(ss / (2 * count) + 1e-30),
    dc: Math.max(Math.abs(dcL), Math.abs(dcR)) / count,
    nonFinite: bad,
  };
}

// 16-bit PCM stereo WAV with TPDF dither.
export function writeWav(path, buf) {
  const N = buf.L.length;
  const data = Buffer.alloc(44 + N * 4);
  data.write('RIFF', 0);
  data.writeUInt32LE(36 + N * 4, 4);
  data.write('WAVE', 8);
  data.write('fmt ', 12);
  data.writeUInt32LE(16, 16);
  data.writeUInt16LE(1, 20);
  data.writeUInt16LE(2, 22);
  data.writeUInt32LE(SR, 24);
  data.writeUInt32LE(SR * 4, 28);
  data.writeUInt16LE(4, 32);
  data.writeUInt16LE(16, 34);
  data.write('data', 36);
  data.writeUInt32LE(N * 4, 40);
  const rng = makeRng(0x5eed);
  let clipped = 0;
  for (let n = 0; n < N; n++) {
    for (let c = 0; c < 2; c++) {
      const x = c === 0 ? buf.L[n] : buf.R[n];
      const d = (rng() + rng()) * 0.5; // TPDF, +-1 LSB
      let v = Math.round(x * 32767 + d);
      if (v > 32767) { v = 32767; clipped++; }
      if (v < -32768) { v = -32768; clipped++; }
      data.writeInt16LE(v, 44 + n * 4 + c * 2);
    }
  }
  writeFileSync(path, data);
  return clipped;
}

export function readWav(path) {
  const b = readFileSync(path);
  let off = 12;
  let fmt = null;
  while (off < b.length) {
    const id = b.toString('ascii', off, off + 4);
    const size = b.readUInt32LE(off + 4);
    if (id === 'fmt ') fmt = {channels: b.readUInt16LE(off + 10), rate: b.readUInt32LE(off + 12), bits: b.readUInt16LE(off + 22)};
    if (id === 'data') {
      const N = size / 4;
      const out = stereo(N);
      for (let n = 0; n < N; n++) {
        out.L[n] = b.readInt16LE(off + 8 + n * 4) / 32768;
        out.R[n] = b.readInt16LE(off + 8 + n * 4 + 2) / 32768;
      }
      return {fmt, buf: out};
    }
    off += 8 + size + (size & 1);
  }
  throw new Error('no data chunk');
}
