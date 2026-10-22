// The System Look's tokens, as in source/app/ui.hpp and docs/DESIGN.md.
// Everything is in console pixels: the top screen is 400 x 240, the bottom
// 320 x 240.

export const TOP_W = 400;
export const BOTTOM_W = 320;
export const SCREEN_H = 240;

// The version the video shows on Home and in Settings.
export const APP_VERSION = '0.8.2';

export const C = {
  TopA: '#F2EBB2',
  TopB: '#F2E9A3',
  BottomA: '#F1E9A3',
  BottomB: '#F0E694',
  Panel: '#DCD49A',
  Recess: '#D9D3A0',
  Dot: '#B0A66E',

  Ink: '#615A4B',
  InkStrong: '#4F493B',
  InkButton: '#756E60',
  InkDialog: '#141414',
  InkSoft: '#8C8466',
  Counter: '#B1A96D',
  Heading: '#63593F',

  Accent: '#1C93C2',
  Tag: '#609CB4',
  Focus: '#3CDC78',
  FocusGlow: '#A0FAA0',
  Badge: '#E69614',
  Good: '#3FA65A',
  Bad: '#E0301A',
  Neutral: '#ACA27A',
  Caution: '#B8621A',

  IconBlue: '#1C93C2',
  IconTeal: '#1E9E84',
  IconRed: '#D2603A',
  IconOrange: '#E9890E',
  IconGrey: '#7D7563',
  IconGreen: '#32A865',
  IconPink: '#C05BA8',
  IconGold: '#C9A227',
} as const;

// citro2d scales of the system font (ui.hpp, namespace type).
export const T = {
  Hero: 0.95,
  Title: 0.78,
  Button: 0.72,
  Tab: 0.6,
  Body: 0.62,
  Small: 0.54,
  Row: 0.52,
  Tiny: 0.44,
} as const;

export type Rect = {x: number; y: number; w: number; h: number};
export const R = (x: number, y: number, w: number, h: number): Rect => ({x, y, w, h});
export const inset = (r: Rect, by: number): Rect => ({x: r.x + by, y: r.y + by, w: r.w - 2 * by, h: r.h - 2 * by});

export type Stop = [at: number, color: string];

// Colour helpers on #RRGGBB strings.
const parse = (hex: string): [number, number, number] => {
  const v = parseInt(hex.slice(1, 7), 16);
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
};
const toHex = (r: number, g: number, b: number) =>
  '#' + [r, g, b].map((n) => Math.max(0, Math.min(255, Math.round(n))).toString(16).padStart(2, '0')).join('');

export const mix = (a: string, b: string, t: number) => {
  const [r1, g1, b1] = parse(a);
  const [r2, g2, b2] = parse(b);
  return toHex(r1 + (r2 - r1) * t, g1 + (g2 - g1) * t, b1 + (b2 - b1) * t);
};
export const darker = (hex: string, by: number) => mix(hex, '#000000', by);
export const rgba = (hex: string, alpha: number) => {
  const [r, g, b] = parse(hex);
  return `rgba(${r},${g},${b},${alpha})`;
};

// --- Easing and time ----------------------------------------------------------

export const clamp01 = (v: number) => Math.max(0, Math.min(1, v));
export const easeOut = (v: number) => 1 - Math.pow(1 - clamp01(v), 3);
export const easeInOut = (v: number) => {
  const x = clamp01(v);
  return x < 0.5 ? 4 * x * x * x : 1 - Math.pow(-2 * x + 2, 3) / 2;
};
export const easeOutBack = (v: number) => {
  const c = 1.4;
  const u = clamp01(v) - 1;
  return 1 + (c + 1) * u * u * u + c * u * u;
};
export const lerp = (a: number, b: number, t: number) => a + (b - a) * t;
export const span = (t: number, from: number, to: number) => clamp01((t - from) / (to - from));

// A small seeded PRNG (mulberry32), so anything random is the same every
// render.
export const prng = (seed: number) => {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
};

export type Ctx = CanvasRenderingContext2D;
