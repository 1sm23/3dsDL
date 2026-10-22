import {clockText} from './draw';
import {ripple} from './kit';
import {setClock} from './clock';
import type {Shot} from './screens';
import {getDeviceScale} from './text';
import {BOTTOM_W, Ctx, easeOut, Rect, SCREEN_H, TOP_W} from './theme';

export type CueKind = 'tap' | 'move' | 'select' | 'back' | 'chime' | 'done' | 'installed' | 'notify' | 'error' | 'type' | 'shutter' | 'boot';
export type ScreenCue = {t: number; kind: CueKind};
export type Tap = {t: number; x: number; y: number; dur?: number};

// How long the stylus stays down on a tap; the button shows pressed for this
// long and acts when it lifts.
export const TAP = 0.15;

type Segment = {at: number; shot: (t: number, local: number) => Shot; fade: number};
type Overlay = (top: Ctx, bottom: Ctx, t: number) => void;

// A sequence: what is on screen from when, its sounds and its touches.
export class Timeline {
  segments: Segment[] = [];
  cues: ScreenCue[] = [];
  taps: Tap[] = [];
  overlays: Overlay[] = [];
  constructor(
    public duration: number,
    // Seconds since midnight on the status bar's clock at t = 0.
    public clockAt: number,
  ) {}

  // From at on, shot draws both screens. Screens cross-fade in fade seconds
  // (100 ms, docs/DESIGN.md); 0 cuts.
  at(at: number, shot: (t: number, local: number) => Shot, fade = 0.1) {
    this.segments.push({at, shot, fade});
    this.segments.sort((a, b) => a.at - b.at);
    return this;
  }

  cue(t: number, kind: CueKind) {
    this.cues.push({t, kind});
    return this;
  }

  // The stylus touches (x, y) at t; a 'tap' cue goes with it.
  tap(t: number, x: number, y: number, dur = TAP) {
    this.taps.push({t, x: Math.round(x * 10) / 10, y: Math.round(y * 10) / 10, dur});
    this.cue(t, 'tap');
    return this;
  }

  tapRect(t: number, r: Rect, dur = TAP) {
    return this.tap(t, r.x + r.w / 2, r.y + r.h / 2, dur);
  }

  overlay(fn: Overlay) {
    this.overlays.push(fn);
    return this;
  }

  // True while the stylus is down on a tap that started at tapAt.
  static pressed(t: number, tapAt: number, dur = TAP) {
    return t >= tapAt && t < tapAt + dur;
  }

  info() {
    return {
      duration: this.duration,
      cues: [...this.cues].sort((a, b) => a.t - b.t),
      taps: [...this.taps].sort((a, b) => a.t - b.t),
    };
  }

  draw(t: number, top: Ctx, bottom: Ctx) {
    const time = Math.max(0, Math.min(t, this.duration));
    setClock(clockText(this.clockAt + time));
    let index = 0;
    for (let i = 0; i < this.segments.length; i++) if (this.segments[i].at <= time) index = i;
    const seg = this.segments[index];
    const local = time - seg.at;
    const shot = seg.shot(time, local);
    const prev = index > 0 ? this.segments[index - 1] : null;
    if (prev && seg.fade > 0 && local < seg.fade) {
      const old = prev.shot(time, time - prev.at);
      const e = easeOut(local / seg.fade);
      blend(top, old.top, shot.top, e, TOP_W, 'top');
      blend(bottom, old.bottom, shot.bottom, e, BOTTOM_W, 'bottom');
    } else {
      shot.top(top);
      shot.bottom(bottom);
    }
    for (const fn of this.overlays) fn(top, bottom, time);
    for (const tap of this.taps) ripple(bottom, tap.x, tap.y, time - tap.t);
  }
}

const fadeLayers = new Map<string, OffscreenCanvas>();

const blend = (ctx: Ctx, from: (c: Ctx) => void, to: (c: Ctx) => void, amount: number, width: number, key: string) => {
  from(ctx);
  const scale = getDeviceScale();
  const w = Math.ceil(width * scale);
  const h = Math.ceil(SCREEN_H * scale);
  let layer = fadeLayers.get(key);
  if (!layer || layer.width !== w || layer.height !== h) {
    layer = new OffscreenCanvas(w, h);
    fadeLayers.set(key, layer);
  }
  const l = layer.getContext('2d') as unknown as Ctx;
  l.setTransform(1, 0, 0, 1, 0, 0);
  l.clearRect(0, 0, w, h);
  l.setTransform(scale, 0, 0, scale, 0, 0);
  to(l);
  ctx.save();
  ctx.globalAlpha = amount;
  ctx.drawImage(layer, 0, 0, width, SCREEN_H);
  ctx.restore();
};
