import {Easing} from 'remotion';
import type {CSSProperties} from 'react';

// Small pure helpers: everything on the page is a function of t.

export const clamp = (x: number, a = 0, b = 1) => Math.min(b, Math.max(a, x));
export const prog = (t: number, a: number, b: number) => (b <= a ? (t >= b ? 1 : 0) : clamp((t - a) / (b - a)));
export const mix = (a: number, b: number, p: number) => a + (b - a) * p;

// The page's own curves: --ease and --spring in remote_page.cpp.
export const EASE = Easing.bezier(0.2, 0.8, 0.2, 1);
export const SPRING = Easing.bezier(0.2, 0.9, 0.3, 1.25);
export const IN_OUT = Easing.bezier(0.45, 0, 0.2, 1);
export const SMOOTH = Easing.bezier(0.42, 0, 0.58, 1);

// keyframes rise{from{opacity:0;transform:translateY(8px)}}
export const rise = (t: number, t0: number, dur = 0.35, dy = 8): CSSProperties => {
  const p = EASE(prog(t, t0, t0 + dur));
  return p >= 1 ? {} : {opacity: p, transform: `translateY(${(1 - p) * dy}px)`};
};

// keyframes pop{from{opacity:0;transform:translateY(10px) scale(.95)}}
export const pop = (t: number, t0: number, dur = 0.28) => {
  const p = SPRING(prog(t, t0, t0 + dur));
  const o = clamp(EASE(prog(t, t0, t0 + dur)));
  return {opacity: o, transform: `translateY(${(1 - p) * 10}px) scale(${0.95 + 0.05 * p})`};
};

// Deterministic noise for idle motion.
export const wobble = (t: number, seed: number) =>
  Math.sin(t * 1.7 + seed * 1.3) * 0.6 + Math.sin(t * 2.9 + seed * 2.1) * 0.3 + Math.sin(t * 0.8 + seed) * 0.4;

// Port of size() in remote_page.cpp.
export function size(b: number): string {
  if (b < 0) return '';
  const units = ['B', 'KiB', 'MiB', 'GiB'];
  let i = 0;
  while (b >= 1024 && i < 3) {
    b /= 1024;
    i++;
  }
  return (i ? b.toFixed(b < 10 ? 2 : 1) : String(b)) + ' ' + units[i];
}

// Port of duration() in remote_page.cpp.
export function duration(s: number): string {
  if (!isFinite(s) || s < 0) return '';
  if (s < 60) return 'about ' + Math.max(1, Math.round(s)) + ' s';
  if (s < 3600) return 'about ' + Math.round(s / 60) + ' min';
  return 'about ' + (s / 3600).toFixed(1) + ' h';
}

export const ext = (n: string) => {
  const i = n.lastIndexOf('.');
  return i > 0 ? n.slice(i + 1).toLowerCase() : '';
};
