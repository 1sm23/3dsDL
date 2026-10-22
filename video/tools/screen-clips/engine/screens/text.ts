import {ROUNDED} from '../lib/fonts';
import {Ctx} from './theme';

// The console draws the system font; M PLUS Rounded 1c stands in for it.
// The numbers below were fitted to text measured in the real captures
// (docs/images): a citro2d scale s draws caps about 20 s tall, lines are
// ceil(42 s) apart, and the system font's glyphs are wider and more evenly
// spaced than M PLUS's, so glyphs are stretched a little, tracked out, and
// digits and the narrowest letters adjusted.
const SIZE = 27.4; // font px per unit of scale
const STRETCH = 1.18; // horizontal glyph stretch
const TRACK = 2.8; // extra advance per glyph, per unit of scale
const DIGIT = 0.7; // digits are wider (tabular) on the console
const NARROW = -4.0; // i, t, l, f, j and r are tighter
const BASELINE = 27.8; // baseline below the text's y, per unit of scale
const NARROW_SET = new Set(['i', 't', 'l', 'f', 'j', 'r']);

export const lineHeight = (s: number) => Math.ceil(s * 42 - 1e-6);

let deviceScale = 3;
export const setDeviceScale = (scale: number) => {
  deviceScale = scale;
};
export const getDeviceScale = () => deviceScale;

const fontFor = (weight: number, px: number) => `${weight} ${px}px ${ROUNDED}`;

// Widths and glyph bitmaps are only kept once the fonts are in; before
// that (a frame drawn too early) they are worked out but not remembered.
let fontsReady = false;
export const setFontsReady = () => {
  fontsReady = true;
};

let measurer: OffscreenCanvasRenderingContext2D | null = null;
const advances = new Map<string, number>();

// The M PLUS advance of one character at 1 px, for a weight.
const baseAdvance = (ch: string, weight: number) => {
  const key = weight + ch;
  const hit = advances.get(key);
  if (hit !== undefined) return hit;
  if (!measurer) {
    measurer = new OffscreenCanvas(8, 8).getContext('2d')!;
  }
  measurer.font = fontFor(weight, 100);
  const value = measurer.measureText(ch).width / 100;
  if (fontsReady) advances.set(key, value);
  return value;
};

const glyphAdvance = (ch: string, s: number, weight: number) => {
  let extra = TRACK;
  if (ch >= '0' && ch <= '9') extra += DIGIT;
  if (NARROW_SET.has(ch)) extra += NARROW;
  return s * (STRETCH * SIZE * baseAdvance(ch, weight) + extra);
};

const chars = (value: string) => Array.from(value);

// How wide the text is drawn.
const drawnWidth = (value: string, s: number, weight = 400) => {
  let w = 0;
  for (const ch of chars(value)) w += glyphAdvance(ch, s, weight);
  return w;
};

// How wide the app thinks text is, for wrapping, fitting and placing it:
// the same as drawn.
const measureFactor = (_s: number) => 1;

export const textWidth = (value: string, s: number, weight = 400) => drawnWidth(value, s, weight) * measureFactor(s);

// --- Bitmaps of strings ----------------------------------------------------------

type Sprite = {canvas: OffscreenCanvas; width: number; padX: number; padTop: number};
const sprites = new Map<string, Sprite>();
const MAX_SPRITES = 2500;

const sprite = (value: string, s: number, color: string, weight: number): Sprite => {
  const key = `${deviceScale}|${weight}|${s}|${color}|${value}`;
  const hit = sprites.get(key);
  if (hit) return hit;
  const list = chars(value);
  const width = drawnWidth(value, s, weight);
  const padX = 3;
  const padTop = 3 + 2 * s;
  const w = Math.max(1, Math.ceil((width + 2 * padX) * deviceScale));
  const h = Math.max(1, Math.ceil((padTop + lineHeight(s) + 4) * deviceScale));
  const canvas = new OffscreenCanvas(w, h);
  const ctx = canvas.getContext('2d')!;
  ctx.font = fontFor(weight, SIZE * s);
  ctx.fillStyle = color;
  ctx.textBaseline = 'alphabetic';
  let x = padX;
  for (const ch of list) {
    if (ch !== ' ') {
      ctx.setTransform(deviceScale * STRETCH, 0, 0, deviceScale, x * deviceScale, (padTop + BASELINE * s) * deviceScale);
      ctx.fillText(ch, 0, 0);
    }
    x += glyphAdvance(ch, s, weight);
  }
  const made = {canvas, width, padX, padTop};
  if (!fontsReady) return made;
  if (sprites.size >= MAX_SPRITES) {
    const first = sprites.keys().next().value;
    if (first !== undefined) sprites.delete(first);
  }
  sprites.set(key, made);
  return made;
};

export type Align = 'left' | 'center' | 'right';

// Draws value with its cell's top at y, like C2D_DrawText.
export const drawText = (ctx: Ctx, value: string, x: number, y: number, s: number, color: string, align: Align = 'left', weight = 400) => {
  if (!value) return;
  const sp = sprite(value, s, color, weight);
  let left = x;
  if (align === 'center') left = x - sp.width / 2;
  else if (align === 'right') left = x - sp.width;
  // Snap to device pixels so the bitmap stays sharp.
  const dx = Math.round((left - sp.padX) * deviceScale) / deviceScale;
  const dy = Math.round((y - sp.padTop) * deviceScale) / deviceScale;
  ctx.drawImage(sp.canvas, dx, dy, sp.canvas.width / deviceScale, sp.canvas.height / deviceScale);
};

export const centeredY = (areaY: number, areaH: number, s: number) => areaY + (areaH - lineHeight(s)) / 2 + 1;

// drawCentered: vertically centred in the rect, horizontally too unless left.
export const drawCentered = (
  ctx: Ctx,
  value: string,
  r: {x: number; y: number; w: number; h: number},
  s: number,
  color: string,
  left = false,
  weight = 400,
) => {
  const y = centeredY(r.y, r.h, s);
  if (left) drawText(ctx, value, r.x, y, s, color, 'left', weight);
  else drawText(ctx, value, r.x + r.w / 2, y, s, color, 'center', weight);
};

// --- Fitting and wrapping ------------------------------------------------------------

const ELLIPSIS = '...';

// Cuts characters from the end until value + "..." fits.
const withEllipsis = (value: string, s: number, maxWidth: number) => {
  maxWidth /= measureFactor(s);
  const ell = drawnWidth(ELLIPSIS, s);
  let w = 0;
  let out = '';
  for (const ch of chars(value)) {
    const a = glyphAdvance(ch, s, 400);
    if (w + a + ell > maxWidth) break;
    w += a;
    out += ch;
  }
  return out.replace(/ +$/, '') + ELLIPSIS;
};

export const fitEnd = (value: string, s: number, maxWidth: number) =>
  textWidth(value, s) <= maxWidth ? value : withEllipsis(value, s, maxWidth);

const isBreakAfter = (ch: string) => ' /?&=-_.,'.includes(ch);

export const wrap = (value: string, s: number, maxWidth: number, maxLines: number): string[] => {
  const limit = maxWidth / measureFactor(s);
  const lines: string[] = [];
  if (maxLines <= 0) return lines;
  let line = '';
  let width = 0;
  let breakAt = 0;
  let cut = false;
  const push = (done: string) => {
    if (lines.length === maxLines) {
      cut = true;
      return;
    }
    lines.push(done.replace(/ +$/, ''));
  };
  for (const ch of chars(value)) {
    if (cut) break;
    if (ch === '\n') {
      push(line);
      line = '';
      width = 0;
      breakAt = 0;
      continue;
    }
    if (ch === ' ' && line === '') continue;
    const a = glyphAdvance(ch, s, 400);
    if (width + a > limit && line !== '') {
      let rest = '';
      if (breakAt > 0 && breakAt < line.length) {
        rest = line.slice(breakAt);
        line = line.slice(0, breakAt);
      }
      push(line);
      line = rest.replace(/^ +/, '');
      width = drawnWidth(line, s);
      breakAt = 0;
      if (cut) break;
      if (ch === ' ' && line === '') continue;
    }
    line += ch;
    width += a;
    if (isBreakAfter(ch)) breakAt = line.length;
  }
  if (!cut && line !== '') push(line);
  if (cut && lines.length) lines[lines.length - 1] = withEllipsis(lines[lines.length - 1], s, maxWidth);
  return lines;
};

// Text too long for its box waits 1.4 s, slides to its end at 32 px a
// second, rests 1.1 s and starts over. since is how long it has been shown.
export const marqueeOffset = (overflow: number, since: number) => {
  const travel = overflow / 32;
  const cycle = 1.4 + travel + 1.1;
  const at = ((since % cycle) + cycle) % cycle;
  if (at < 1.4) return 0;
  if (at >= 1.4 + travel) return overflow;
  return (at - 1.4) * 32;
};

export const drawMarquee = (
  ctx: Ctx,
  value: string,
  x: number,
  y: number,
  s: number,
  color: string,
  maxWidth: number,
  align: Align = 'left',
  since = 0,
) => {
  const width = textWidth(value, s);
  if (width <= maxWidth + 0.5) {
    drawText(ctx, value, x, y, s, color, align);
    return;
  }
  const left = align === 'center' ? x - maxWidth / 2 : align === 'right' ? x - maxWidth : x;
  const offset = marqueeOffset(width - maxWidth, since);
  ctx.save();
  ctx.beginPath();
  ctx.rect(left, y - 2, maxWidth, lineHeight(s) + 6);
  ctx.clip();
  drawText(ctx, value, left - offset, y, s, color, 'left');
  ctx.restore();
};

export const drawCenteredMarquee = (
  ctx: Ctx,
  value: string,
  r: {x: number; y: number; w: number; h: number},
  s: number,
  color: string,
  left = false,
  since = 0,
) => {
  const y = centeredY(r.y, r.h, s);
  if (left) drawMarquee(ctx, value, r.x, y, s, color, r.w, 'left', since);
  else drawMarquee(ctx, value, r.x + r.w / 2, y, s, color, r.w, 'center', since);
};

// Wrapped text, top-down from y; returns the y below it. Text that runs out
// of lines scrolls the rest on the last line.
export const drawWrapped = (
  ctx: Ctx,
  value: string,
  x: number,
  y: number,
  s: number,
  color: string,
  maxWidth: number,
  maxLines: number,
  align: Align = 'left',
  since = 0,
) => {
  const step = lineHeight(s);
  const all = wrap(value, s, maxWidth, 4096);
  let shown = all;
  let lastScrolls = false;
  if (all.length > maxLines && maxLines > 0) {
    // The last line holds everything that did not fit, to scroll, cut from
    // the original text so nothing is added at the breaks.
    shown = all.slice(0, maxLines - 1);
    let position = 0;
    for (const line of shown) {
      const found = value.indexOf(line, position);
      if (found >= 0) position = found + line.length;
    }
    shown.push(value.slice(position).replace(/[\r\n]/g, ' ').replace(/^ +/, ''));
    lastScrolls = true;
  }
  shown.forEach((line, index) => {
    if (lastScrolls && index === shown.length - 1) drawMarquee(ctx, line, x, y, s, color, maxWidth, align, since);
    else drawText(ctx, line, x, y, s, color, align);
    y += step;
  });
  return y;
};
