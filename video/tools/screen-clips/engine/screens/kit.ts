import {
  badge,
  BL,
  bottomBackground,
  BR,
  button,
  DANGER_STRIP_H,
  DIALOG_BOX,
  dialogBox,
  drawCentered,
  drawMarquee,
  drawShadow,
  drawText,
  drawWrapped,
  fillRect,
  fillRound,
  fitEnd,
  focusRing,
  IconPainter,
  lineHeight,
  panel,
  panelText,
  progressBar,
  recess,
  spinner,
  STRIP_H,
  strip,
  strokeRound,
  Style,
  tab,
  TabSide,
  tabRect,
  textWidth,
  tile,
  TOP_PANEL,
  topChrome,
  wrap,
} from './draw';
import {drawIcon, IconName} from './icons';
import {drawCenteredMarquee} from './text';
import {getClock} from './clock';
import {BOTTOM_W, C, Ctx, easeOut, inset, lerp, R, Rect, rgba, SCREEN_H, T, TOP_W} from './theme';

// Screen-level pieces of the toolkit, drawn from a description and a little
// state (what is focused, pressed, scrolled), so a sequence can say what is
// on screen at any time without keeping anything between frames.

export type Painter = (ctx: Ctx) => void;

// --- Focus that glides --------------------------------------------------------------------

export const RING_GLIDE = 0.12;

export type FocusTrack<Id> = {id: Id; prev: Id | null; since: number};

// Which control is focused at t, from [time, id] keys in order.
export const focusAt = <Id>(keys: [number, Id][], t: number): FocusTrack<Id> => {
  let index = 0;
  for (let i = 0; i < keys.length; i++) if (keys[i][0] <= t) index = i;
  return {id: keys[index][1], prev: index > 0 ? keys[index - 1][1] : null, since: t - keys[index][0]};
};

// --- Screens of controls (ui::runScreen) --------------------------------------------------

export type Control = {
  rect: Rect;
  label: string;
  icon?: IconName;
  iconColor?: string;
  tile?: boolean;
  style?: Style;
  scale?: number;
  enabled?: boolean;
  labelColor?: string;
  badge?: string;
  badgeColor?: string;
};

export type ScreenSpec = {
  controls: Control[];
  back?: string;
  action?: string;
  actionKey?: string;
  closeBar?: boolean;
  drawBottom?: Painter;
};

export type FocusId = number | 'back' | 'action' | null;

const controlRadius = (c: Control) => (c.tile ? 10 : c.rect.h >= 34 ? 9 : 8);

export type ControlsState = {
  focus: FocusTrack<FocusId>;
  pressed?: FocusId;
  t: number;
};

export const controlsBottom = (ctx: Ctx, spec: ScreenSpec, st: ControlsState) => {
  bottomBackground(ctx);
  spec.drawBottom?.(ctx);
  spec.controls.forEach((c, index) => {
    if (c.enabled === false) return;
    const down = st.pressed === index;
    if (c.tile) tile(ctx, c.rect, c.label, c.icon!, c.iconColor ?? C.IconGrey, down);
    else button(ctx, c.rect, c.label, {style: c.style, scale: c.scale, icon: c.icon, iconColor: c.iconColor, pressed: down, labelColor: c.labelColor});
  });
  spec.controls.forEach((c) => {
    if (c.enabled === false) button(ctx, c.rect, c.label, {style: 'disabled', scale: c.scale});
  });
  const id = st.focus.id;
  if (spec.closeBar) {
    tab(ctx, 'full', 'Close', 'START', id === 'back', st.pressed === 'back');
  } else {
    if (spec.back) tab(ctx, 'left', spec.back, '', id === 'back', st.pressed === 'back');
    if (spec.action) tab(ctx, 'right', spec.action, spec.actionKey ?? '', id === 'action', st.pressed === 'action');
  }
  if (typeof id === 'number') {
    const c = spec.controls[id];
    let rect = c.rect;
    let radius = controlRadius(c);
    const prev = st.focus.prev;
    if (typeof prev === 'number' && prev !== id && st.focus.since < RING_GLIDE) {
      const p = spec.controls[prev];
      const e = easeOut(st.focus.since / RING_GLIDE);
      rect = R(lerp(p.rect.x, c.rect.x, e), lerp(p.rect.y, c.rect.y, e), lerp(p.rect.w, c.rect.w, e), lerp(p.rect.h, c.rect.h, e));
      radius = lerp(controlRadius(p), radius, e);
    }
    focusRing(ctx, rect, radius, st.t);
  }
  for (const c of spec.controls) {
    if (!c.badge) continue;
    const width = Math.max(16, textWidth(c.badge, 0.34) + 10);
    const right = Math.min(c.rect.x + c.rect.w + 6, BOTTOM_W - 2);
    const top = Math.max(1.5, c.rect.y - 9);
    fillRound(ctx, R(right - width - 1.5, top - 1.5, width + 3, 19), 5.5, '#FFFFFF');
    const b = R(right - width, top, width, 16);
    fillRound(ctx, b, 4, c.badgeColor ?? C.Badge);
    drawCentered(ctx, c.badge, b, 0.34, '#FFFFFF');
  }
};

// --- Lists (ui::runList) --------------------------------------------------------------------

export type ListItem = {
  label: string;
  detail?: string;
  labelColor?: string;
  marked?: boolean;
  icon?: IconName;
  iconColor?: string;
  badge?: string;
  badgeColor?: string;
};

export type ListSpec = {
  title?: string;
  items: ListItem[];
  emptyText?: string;
  back?: string;
  action?: string;
  actionKey?: string;
  hint?: string;
};

const ROW_H = 25;
const PITCH = 31;

export const listGeometry = (spec: {title?: string; items: unknown[]}) => {
  const titled = !!spec.title;
  const box = R(8, titled ? 21 : 6, 304, titled ? 183 : 198);
  const inner = R(box.x, box.y + 4, box.w, box.h - 8);
  const count = spec.items.length;
  const content = count * PITCH + 2;
  const viewport = inner.h;
  const scrollable = content > viewport;
  return {box, inner, content, viewport, scrollable, rowLeft: box.x + 7, rowWidth: box.w - 14 - (scrollable ? 12 : 0)};
};

// Where row index sits on the touch screen at a scroll offset.
export const rowRect = (spec: ListSpec, index: number, offset = 0) => {
  const g = listGeometry(spec);
  return R(g.rowLeft, g.inner.y + 2 + index * PITCH - offset, g.rowWidth, ROW_H);
};

const SCROLL_EASE = 0.16;

// The cursor and scroll offset at t, from [time, cursor] keys: the list
// opens with the first key's cursor and reveals each new one as the app's
// scroller does, easing there.
export const listMotion = (spec: ListSpec, keys: [number, number][], t: number) => {
  const g = listGeometry(spec);
  const max = Math.max(0, g.content - g.viewport);
  const first = keys[0][1];
  let target = first > 0 ? Math.max(0, Math.min(first * PITCH - g.viewport / 2 + PITCH / 2, max)) : 0;
  let shown = target;
  let index = 0;
  for (let i = 1; i < keys.length; i++) {
    if (keys[i][0] > t) break;
    index = i;
    const from = shown;
    const top = keys[i][1] * PITCH;
    let goal = target;
    if (top < goal) goal = top;
    else if (top + PITCH > goal + g.viewport) goal = top + PITCH - g.viewport;
    target = Math.max(0, Math.min(goal, max));
    const nextAt = i + 1 < keys.length ? keys[i + 1][0] : Infinity;
    const until = Math.min(t, nextAt);
    shown = lerp(from, target, easeOut((until - keys[i][0]) / SCROLL_EASE));
  }
  return {
    cursor: keys[index][1],
    prev: index > 0 ? keys[index - 1][1] : null,
    since: t - keys[index][0],
    offset: shown,
  };
};

export type ListState = {
  cursor: number;
  prev?: number | null;
  since?: number;
  offset?: number;
  held?: number; // a row pressed by the stylus
  pressed?: 'back' | 'action' | null;
  ring?: boolean;
  t: number;
};

export const listBottom = (ctx: Ctx, spec: ListSpec, st: ListState) => {
  const g = listGeometry(spec);
  const offset = st.offset ?? 0;
  bottomBackground(ctx);
  if (spec.title) drawMarquee(ctx, spec.title, 160, 2, T.Small, C.Heading, 296, 'center');
  recess(ctx, g.box);
  if (!spec.items.length && spec.emptyText) panelText(ctx, spec.emptyText, g.box, T.Small, C.InkSoft);
  ctx.save();
  ctx.beginPath();
  ctx.rect(g.inner.x, g.inner.y, g.inner.w, g.inner.h);
  ctx.clip();
  spec.items.forEach((item, index) => {
    const row = rowRect(spec, index, offset);
    if (row.y > g.inner.y + g.inner.h || row.y + ROW_H < g.inner.y - 4) return;
    const down = index === st.held;
    fillRound(ctx, R(row.x - 1, row.y - 1, row.w + 2, row.h + 2.5), 9, [
      [0, '#C3BA92'],
      [1, '#A69C6E'],
    ]);
    if (item.marked) {
      fillRound(ctx, row, 8, [
        [0, '#F3FAFF'],
        [1, '#D8EEF8'],
      ]);
    } else {
      fillRound(ctx, row, 8, [
        [0, down ? '#E6E2D2' : '#FEFCF4'],
        [1, down ? '#D8D3BF' : '#EEEBDF'],
      ]);
    }
    let textX = row.x + 8;
    if (item.icon) {
      drawIcon(ctx, item.icon, textX, row.y + 4, 17, item.iconColor ?? C.IconGrey);
      textX += 23;
    }
    let right = row.x + row.w - 8;
    if (item.badge) {
      right -= badge(ctx, right, row.y + 4.5, item.badge, item.badgeColor) + 6;
    } else if (item.detail) {
      const detail = fitEnd(item.detail, T.Tiny, 110);
      const w = textWidth(detail, T.Tiny);
      drawCentered(ctx, detail, R(right - w, row.y, 120, row.h), T.Tiny, C.InkSoft, true);
      right -= w + 8;
    }
    const color = item.labelColor ?? C.InkButton;
    if (index === st.cursor) {
      drawCenteredMarquee(ctx, item.label, R(textX, row.y, right - textX, row.h), T.Row, color, true, st.since ?? 0);
    } else {
      drawCentered(ctx, fitEnd(item.label, T.Row, right - textX), R(textX, row.y, right - textX, row.h), T.Row, color, true);
    }
  });
  if (st.ring !== false && st.cursor >= 0 && st.cursor < spec.items.length) {
    let ring = rowRect(spec, st.cursor, offset);
    if (st.prev != null && st.prev !== st.cursor && (st.since ?? 1) < RING_GLIDE) {
      const from = rowRect(spec, st.prev, offset);
      ring = {...ring, y: lerp(from.y, ring.y, easeOut((st.since ?? 1) / RING_GLIDE))};
    }
    focusRing(ctx, ring, 8, st.t, 2.5, 2.5);
  }
  ctx.restore();
  if (g.scrollable) {
    const trackX = g.box.x + g.box.w - 12;
    fillRect(ctx, trackX + 4, g.inner.y + 4, 2, g.inner.h - 8, '#A79D70');
    const thumb = Math.max(18, ((g.inner.h - 8) * g.viewport) / g.content);
    const travel = g.inner.h - 8 - thumb;
    const position = Math.max(0, Math.min(1, offset / (g.content - g.viewport)));
    const bar = R(trackX, g.inner.y + 4 + travel * position, 10, thumb);
    fillRound(ctx, R(bar.x - 1, bar.y - 1, bar.w + 2, bar.h + 2), 5, '#9A916A');
    fillRound(ctx, bar, 4, '#FBF9F0');
  }
  if (spec.back !== '') tab(ctx, 'left', spec.back ?? 'Back', '', false, st.pressed === 'back');
  if (spec.action) tab(ctx, 'right', spec.action, spec.actionKey ?? '', false, st.pressed === 'action');
  if (spec.hint) drawMarquee(ctx, spec.hint, 160, 220, T.Tiny, C.Counter, 60, 'center');
};

// --- Dialogs (ui::choose) -------------------------------------------------------------------

export type Tone = 'info' | 'success' | 'warning' | 'error';

export type Paragraph = {text: string; color?: string; maxLines?: number};

export type DialogSpec = {
  header: string;
  title: string;
  tone?: Tone;
  body?: Paragraph[];
  buttons?: string[];
  dangerous?: number;
  icon?: IconPainter;
};

const toneIcon = (tone: Tone = 'info'): IconName => (tone === 'error' || tone === 'warning' ? 'Warning' : tone === 'success' ? 'Check' : 'AppMark');
const toneColor = (tone: Tone = 'info') =>
  tone === 'error' ? C.Bad : tone === 'warning' ? C.IconOrange : tone === 'success' ? C.Good : C.Accent;

export const titleCase = (value: string) => value.replace(/(^|\s)([a-z])/g, (_, a, b) => a + b.toUpperCase());

// The top screen while a dialog is open. Without explanations it keeps
// showing what was there (under).
export const dialogTop = (ctx: Ctx, d: DialogSpec, under?: Painter) => {
  if (!(d.body && d.body.length) && !d.icon) {
    under?.(ctx);
    return;
  }
  topChrome(ctx, getClock(), titleCase(d.header), toneIcon(d.tone), toneColor(d.tone));
  panel(ctx);
  let y = TOP_PANEL.y + 12;
  let left = TOP_PANEL.x + 18;
  let width = TOP_PANEL.w - 36;
  if (d.icon) {
    drawShadow(ctx, R(left, y + 1.5, 48, 48), 8, 2, '#403810', 0x50 / 255);
    d.icon(ctx, left, y);
    left += 60;
    width -= 60;
  }
  const bottom = TOP_PANEL.y + TOP_PANEL.h - 8;
  const body = (d.body ?? []).filter((p) => p.text);
  let shrink = 1;
  for (; shrink > 0.8; shrink -= 0.05) {
    let height = 0;
    body.forEach((p, i) => {
      const s = (i === 0 ? T.Body : T.Small) * shrink;
      height += wrap(p.text, s, width, (p.maxLines ?? 3) + 1).length * lineHeight(s) + 5;
    });
    if (y + height <= bottom) break;
  }
  body.forEach((p, i) => {
    const s = (i === 0 ? T.Body : T.Small) * shrink;
    const room = Math.floor(Math.max(0, (bottom - y) / lineHeight(s)));
    const lines = Math.min((p.maxLines ?? 3) + 1, room);
    if (lines === 0) return;
    y = drawWrapped(ctx, p.text, left, y, s, p.color ?? C.Ink, width, lines) + 5;
    if (y > TOP_PANEL.y + 62) {
      left = TOP_PANEL.x + 18;
      width = TOP_PANEL.w - 36;
    }
  });
};

// How a dialog arrives: the screen dims in 120 ms and the box grows from
// 92% in 140 ms (docs/DESIGN.md, Motion).
const DIM = 'rgba(20,18,8,0.478)';

const layers = new Map<string, OffscreenCanvas>();
const layerFor = (w: number, h: number) => {
  const key = `${w}x${h}`;
  let canvas = layers.get(key);
  if (!canvas) {
    canvas = new OffscreenCanvas(w, h);
    layers.set(key, canvas);
  }
  const lctx = canvas.getContext('2d')!;
  lctx.setTransform(1, 0, 0, 1, 0, 0);
  lctx.clearRect(0, 0, w, h);
  return {canvas, ctx: lctx as unknown as Ctx};
};

// Draws paint (the dialog box and what is on it) growing in around centre.
const popIn = (ctx: Ctx, age: number, centre: {x: number; y: number}, paint: Painter) => {
  const grow = easeOut(age / 0.14);
  if (grow >= 1) {
    paint(ctx);
    return;
  }
  const m = ctx.getTransform();
  const w = ctx.canvas.width;
  const h = ctx.canvas.height;
  const {canvas, ctx: l} = layerFor(w, h);
  l.setTransform(m);
  paint(l);
  const scale = 0.92 + 0.08 * grow;
  ctx.save();
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.globalAlpha = Math.min(1, 0.3 + grow);
  const cx = centre.x * m.a + m.e;
  const cy = centre.y * m.d + m.f;
  ctx.translate(cx, cy);
  ctx.scale(scale, scale);
  ctx.translate(-cx, -cy);
  ctx.drawImage(canvas, 0, 0);
  ctx.restore();
};

export const dim = (ctx: Ctx, age: number) => {
  ctx.fillStyle = DIM;
  ctx.save();
  ctx.globalAlpha = easeOut(age / 0.12);
  ctx.fillRect(0, 0, BOTTOM_W, SCREEN_H);
  ctx.restore();
};

export type DialogState = {selected: number; pressed?: number; age: number; t: number};

export const dialogBottom = (ctx: Ctx, d: DialogSpec, st: DialogState, under: Painter) => {
  under(ctx);
  dim(ctx, st.age);
  const labels = d.buttons && d.buttons.length ? d.buttons : ['OK'];
  const red = d.dangerous != null && d.dangerous >= 0 && labels.length === 2;
  const box = DIALOG_BOX;
  popIn(ctx, st.age, {x: box.x + box.w / 2, y: box.y + box.h / 2}, (c) => {
    dialogBox(c, box);
    const stripTop = box.y + box.h - (red ? DANGER_STRIP_H + 56 : STRIP_H);
    const message = R(box.x + 16, box.y + 8, box.w - 32, stripTop - box.y - 12);
    const lines = wrap(d.title, T.Body, message.w, 6);
    const step = lineHeight(T.Body) * 1.08;
    let y = message.y + (message.h - step * lines.length) / 2;
    for (const line of lines) {
      drawText(c, line, message.x + message.w / 2, y, T.Body, C.InkDialog, 'center');
      y += step;
    }
    if (red) {
      const danger = d.dangerous!;
      const safe = 1 - danger;
      const redButton = R(box.x + 18, box.y + box.h - DANGER_STRIP_H - 48, box.w - 36, 36);
      button(c, redButton, labels[danger], {style: 'danger', pressed: st.pressed === danger});
      if (st.selected === danger) focusRing(c, redButton, 9, st.t);
      strip(c, box, [labels[safe]], st.selected === safe ? 0 : -1, st.pressed === safe ? 0 : -1, true, DANGER_STRIP_H, -1);
    } else {
      strip(c, box, labels, st.selected, st.pressed ?? -1, false, STRIP_H, d.dangerous ?? -1);
    }
  });
};

// Where a dialog's buttons are on the touch screen, for taps.
export const dialogButtonRect = (d: DialogSpec, index: number) => {
  const labels = d.buttons && d.buttons.length ? d.buttons : ['OK'];
  const box = DIALOG_BOX;
  if (d.dangerous != null && d.dangerous >= 0 && labels.length === 2) {
    if (index === d.dangerous) return R(box.x + 18, box.y + box.h - DANGER_STRIP_H - 48, box.w - 36, 36);
    return R(box.x, box.y + box.h - DANGER_STRIP_H, box.w, DANGER_STRIP_H);
  }
  const width = box.w / labels.length;
  return R(box.x + index * width, box.y + box.h - STRIP_H, width, STRIP_H);
};

// --- Please wait (ui::drawBusy) --------------------------------------------------------------

export const busyTop = (ctx: Ctx, header: string, title: string, under?: Painter) => {
  if (!title) {
    under?.(ctx);
    return;
  }
  topChrome(ctx, getClock(), titleCase(header), 'AppMark', C.Accent);
  panel(ctx);
  panelText(ctx, title, TOP_PANEL);
};

export const busyBottom = (ctx: Ctx, under: Painter, age: number, t: number) => {
  under(ctx);
  dim(ctx, age);
  const box = R(10, 62, 300, 116);
  popIn(ctx, age, {x: 160, y: 120}, (c) => {
    dialogBox(c, box);
    const width = 30 + 12 + textWidth('Please wait...', T.Body);
    spinner(c, box.x + (box.w - width) / 2 + 15, box.y + box.h / 2, 15, t);
    drawCentered(c, 'Please wait...', R(box.x + (box.w - width) / 2 + 42, box.y, 200, box.h), T.Body, C.InkDialog, true);
  });
};

// --- Progress (ui::drawProgress) ---------------------------------------------------------------

export type ProgressView = {
  title: string;
  icon: IconName;
  iconColor: string;
  name: string;
  source: string;
  titleIcon?: IconPainter;
  fraction: number;
  amounts: string;
  speed: string;
  tag?: string;
  note?: string;
  message: string;
  detail: string;
  stopping?: boolean;
};

export const PROGRESS_CANCEL = R(70, 148, 180, 36);

export const progressTop = (ctx: Ctx, v: ProgressView, t: number) => {
  topChrome(ctx, getClock(), v.title, v.icon, v.iconColor);
  panel(ctx);
  const left = TOP_PANEL.x + 18;
  let nameX = left;
  if (v.titleIcon) {
    titleIconFramed(ctx, left, TOP_PANEL.y + 12, v.titleIcon);
    nameX += 60;
  } else {
    drawIcon(ctx, 'Package', left, TOP_PANEL.y + 16, 30, C.Accent);
    nameX += 40;
  }
  const nameW = TOP_PANEL.x + TOP_PANEL.w - 18 - nameX;
  drawMarquee(ctx, v.name, nameX, TOP_PANEL.y + 13, 0.62, C.InkStrong, nameW);
  if (v.note) drawMarquee(ctx, v.note, nameX, TOP_PANEL.y + 41, T.Small, C.Good, nameW);
  else drawMarquee(ctx, v.source, nameX, TOP_PANEL.y + 41, T.Small, C.InkSoft, nameW);
  progressBar(ctx, R(left, TOP_PANEL.y + 72, TOP_PANEL.w - 36, 18), v.fraction, t);
  drawText(ctx, v.amounts, left, TOP_PANEL.y + 98, T.Body, C.Ink);
  if (v.fraction >= 0) {
    drawText(ctx, `${Math.floor(v.fraction * 100 + 1e-6)}%`, TOP_PANEL.x + TOP_PANEL.w - 18, TOP_PANEL.y + 98, T.Body, C.InkStrong, 'right');
  }
  const tagW = v.tag ? textWidth(v.tag, 0.4) + 16 : 0;
  drawMarquee(ctx, v.speed, left, TOP_PANEL.y + 126, T.Small, C.Ink, TOP_PANEL.w - 36 - tagW - 8);
  if (v.tag) {
    fillRound(ctx, R(TOP_PANEL.x + TOP_PANEL.w - 18 - tagW, TOP_PANEL.y + 124, tagW, 20), 5, C.Tag);
    drawCentered(ctx, v.tag, R(TOP_PANEL.x + TOP_PANEL.w - 18 - tagW, TOP_PANEL.y + 124, tagW, 20), 0.4, '#FFFFFF');
  }
};

export const progressBottom = (ctx: Ctx, v: ProgressView, t: number, pressed = false) => {
  bottomBackground(ctx);
  const p = R(13, 10, 294, 128);
  panel(ctx, p);
  let lines: string[];
  let pathLine = false;
  const path = v.message.indexOf(' sdmc:/');
  if (path >= 0 && textWidth(v.message, T.Body) > 266) {
    lines = [v.message.slice(0, path), v.message.slice(path + 1)];
    pathLine = true;
  } else {
    // A little room past 266: the stand-in font runs slightly wider than
    // the console's on some lines, and the console's own fit on two.
    lines = wrap(v.message, T.Body, 296, 2);
  }
  const more = wrap(v.detail, T.Tiny, 266, 3);
  const gap = more.length ? 8 : 0;
  const block = lines.length * lineHeight(T.Body) + gap + more.length * lineHeight(T.Tiny);
  let y = p.y + (p.h - block) / 2;
  lines.forEach((line, i) => {
    if (pathLine && i === 1) drawMarquee(ctx, line, 160, y, T.Body, C.Ink, 266, 'center');
    else drawText(ctx, line, 160, y, T.Body, C.Ink, 'center');
    y += lineHeight(T.Body);
  });
  y += gap;
  for (const line of more) {
    drawText(ctx, line, 160, y, T.Tiny, C.InkSoft, 'center');
    y += lineHeight(T.Tiny);
  }
  if (v.stopping) {
    button(ctx, PROGRESS_CANCEL, 'Stopping...', {style: 'disabled'});
  } else {
    button(ctx, PROGRESS_CANCEL, 'Cancel', {pressed});
    focusRing(ctx, PROGRESS_CANCEL, 9, t);
  }
};

// The framed 48 px icon (ui::drawTitleIcon with a picture).
export const titleIconFramed = (ctx: Ctx, x: number, y: number, painter: IconPainter) => {
  drawShadow(ctx, R(x, y + 1.5, 48, 48), 8, 2, '#403810', 0x50 / 255);
  fillRound(ctx, R(x - 1, y - 1, 50, 50), 9, '#B3AA83');
  fillRound(ctx, R(x, y, 48, 48), 8, '#FFFFFF');
  painter(ctx, x, y);
};

// --- The software keyboard ----------------------------------------------------------------------

// While a line or a name is typed, the console's own keyboard applet takes
// over both screens. This is a plain stand-in for it: what is typed on top,
// keys below.
export const KEY_ROWS = ['1234567890', 'qwertyuiop', 'asdfghjkl', 'zxcvbnm'];
const KEY_W = 26;
const KEY_H = 26;
const KEY_GAP = 3;
const KEYS_TOP = 58;

export const keyRect = (ch: string): Rect | null => {
  const lower = ch.toLowerCase();
  if (ch === ' ') return R(84, KEYS_TOP + 4 * (KEY_H + KEY_GAP), 152, KEY_H);
  for (let row = 0; row < KEY_ROWS.length; row++) {
    const at = KEY_ROWS[row].indexOf(lower);
    if (at < 0) continue;
    const count = KEY_ROWS[row].length;
    const width = count * KEY_W + (count - 1) * KEY_GAP;
    const left = (BOTTOM_W - width) / 2;
    return R(left + at * (KEY_W + KEY_GAP), KEYS_TOP + row * (KEY_H + KEY_GAP), KEY_W, KEY_H);
  }
  return null;
};

export const KEYBOARD_OK = R(214, 206, 96, 28);
export const KEYBOARD_CANCEL = R(10, 206, 96, 28);

const kbKey = (ctx: Ctx, r: Rect, label: string, down: boolean, s = 0.5) => {
  fillRound(ctx, R(r.x, r.y + 1, r.w, r.h), 5, '#1A1C1F');
  const face = down ? R(r.x, r.y + 1, r.w, r.h) : r;
  fillRound(ctx, face, 5, down ? [[0, '#7FC9EC'], [1, '#3FA3D4']] : [[0, '#FDFDFD'], [1, '#DADCE0']]);
  drawCentered(ctx, label, down ? R(r.x, r.y + 1, r.w, r.h) : r, s, down ? '#FFFFFF' : '#3A3D42');
};

export const keyboardTop = (ctx: Ctx, hint: string, typed: string, under: Painter, age: number, t: number) => {
  under(ctx);
  ctx.save();
  ctx.globalAlpha = easeOut(age / 0.12);
  ctx.fillStyle = 'rgba(16,18,22,0.72)';
  ctx.fillRect(0, 0, TOP_W, SCREEN_H);
  const field = R(24, 92, 352, 44);
  drawText(ctx, hint, field.x + 4, field.y - 26, T.Small, '#E6E8EC');
  fillRound(ctx, R(field.x, field.y + 2, field.w, field.h), 8, '#0B0C0E');
  fillRound(ctx, field, 8, [[0, '#FFFFFF'], [1, '#EEF0F3']]);
  const s = T.Small;
  const maxW = field.w - 24;
  const width = textWidth(typed, s);
  const shift = Math.max(0, width - maxW);
  ctx.beginPath();
  ctx.rect(field.x + 10, field.y, field.w - 20, field.h);
  ctx.clip();
  const y = field.y + (field.h - lineHeight(s)) / 2 + 1;
  drawText(ctx, typed, field.x + 12 - shift, y, s, '#202226');
  if (Math.floor(t * 2.2) % 2 === 0) fillRect(ctx, field.x + 13 + width - shift, field.y + 11, 1.6, field.h - 22, C.Accent);
  ctx.restore();
};

export const keyboardBottom = (ctx: Ctx, pressedKey: string | null, okPressed: boolean, age: number, typed: string) => {
  ctx.save();
  ctx.globalAlpha = easeOut(age / 0.12);
  const g = ctx.createLinearGradient(0, 0, 0, SCREEN_H);
  g.addColorStop(0, '#3B3F45');
  g.addColorStop(1, '#25282C');
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, BOTTOM_W, SCREEN_H);
  // The line being typed, small, as the applet repeats it over the keys.
  fillRound(ctx, R(10, 10, 300, 36), 7, '#16181B');
  ctx.save();
  ctx.beginPath();
  ctx.rect(16, 10, 288, 36);
  ctx.clip();
  const w = textWidth(typed, T.Small);
  drawText(ctx, typed, 20 - Math.max(0, w - 276), 10 + (36 - lineHeight(T.Small)) / 2 + 1, T.Small, '#E9ECF0');
  ctx.restore();
  for (const row of KEY_ROWS) {
    for (const ch of row) {
      const r = keyRect(ch)!;
      kbKey(ctx, r, ch, pressedKey === ch);
    }
  }
  kbKey(ctx, keyRect(' ')!, 'Space', pressedKey === ' ', 0.44);
  kbKey(ctx, R(16, KEYS_TOP + 4 * (KEY_H + KEY_GAP), 60, KEY_H), 'Shift', false, 0.44);
  kbKey(ctx, R(244, KEYS_TOP + 4 * (KEY_H + KEY_GAP), 60, KEY_H), 'Delete', false, 0.44);
  kbKey(ctx, KEYBOARD_CANCEL, 'Cancel', false, 0.5);
  kbKey(ctx, KEYBOARD_OK, 'OK', okPressed, 0.5);
  ctx.restore();
};

// --- Touch feedback -------------------------------------------------------------------------------

// A soft ring where the stylus touched, growing and fading for 0.3 s.
export const ripple = (ctx: Ctx, x: number, y: number, age: number) => {
  if (age < 0 || age > 0.3) return;
  const v = age / 0.3;
  const r = 3 + 11 * easeOut(v);
  const a = (1 - v) * 0.7;
  ctx.save();
  ctx.lineWidth = 2.2;
  ctx.strokeStyle = rgba('#FFFFFF', a);
  ctx.beginPath();
  ctx.arc(x, y, r, 0, Math.PI * 2);
  ctx.stroke();
  ctx.lineWidth = 1;
  ctx.strokeStyle = rgba('#4F493B', a * 0.45);
  ctx.beginPath();
  ctx.arc(x, y, r + 1.4, 0, Math.PI * 2);
  ctx.stroke();
  ctx.fillStyle = rgba('#FFFFFF', a * 0.25);
  ctx.beginPath();
  ctx.arc(x, y, r, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
};

export const centreOf = (r: Rect) => ({x: r.x + r.w / 2, y: r.y + r.h / 2});

export {tabRect, inset, strokeRound, BL, BR};
export type {TabSide};
