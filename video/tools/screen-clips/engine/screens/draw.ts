import QRCode from 'qrcode';
import {drawIcon, IconName} from './icons';
import {
  centeredY,
  drawCentered,
  drawCenteredMarquee,
  drawMarquee,
  drawText,
  drawWrapped,
  fitEnd,
  lineHeight,
  textWidth,
  wrap,
} from './text';
import {BOTTOM_W, C, Ctx, darker, inset, R, Rect, rgba, SCREEN_H, Stop, T, TOP_W} from './theme';

// The toolkit's components (source/app/ui.cpp), drawn with canvas paths in
// console pixels.

export const TL = 1;
export const TR = 2;
export const BL = 4;
export const BR = 8;
export const TOP = TL | TR;
export const ALL = 15;

export const roundPath = (ctx: Ctx, r: Rect, radius: number, corners = ALL) => {
  const rad = Math.max(0, Math.min(radius, Math.min(r.w, r.h) / 2));
  ctx.beginPath();
  ctx.roundRect(r.x, r.y, r.w, r.h, [
    corners & TL ? rad : 0,
    corners & TR ? rad : 0,
    corners & BR ? rad : 0,
    corners & BL ? rad : 0,
  ]);
};

const gradient = (ctx: Ctx, r: Rect, stops: Stop[]) => {
  const g = ctx.createLinearGradient(0, r.y, 0, r.y + r.h);
  for (const [at, color] of stops) g.addColorStop(Math.max(0, Math.min(1, at)), color);
  return g;
};

export const fillRound = (ctx: Ctx, r: Rect, radius: number, fill: string | Stop[], corners = ALL) => {
  if (r.w <= 0 || r.h <= 0) return;
  roundPath(ctx, r, radius, corners);
  ctx.fillStyle = typeof fill === 'string' ? fill : gradient(ctx, r, fill);
  ctx.fill();
};

export const fillRect = (ctx: Ctx, x: number, y: number, w: number, h: number, fill: string) => {
  if (w <= 0 || h <= 0) return;
  ctx.fillStyle = fill;
  ctx.fillRect(x, y, w, h);
};

// A ring of width inside r. openBottom leaves out the bottom edge and
// corners, for shapes that run off the screen.
export const strokeRound = (
  ctx: Ctx,
  r: Rect,
  radius: number,
  width: number,
  color: string,
  corners = ALL,
  openBottom = false,
) => {
  if (r.w <= 0 || r.h <= 0) return;
  ctx.save();
  let shape = r;
  if (openBottom) {
    ctx.beginPath();
    ctx.rect(r.x - 1, r.y - 1, r.w + 2, r.h + 1);
    ctx.clip();
    shape = {...r, h: r.h + radius + width + 2};
  }
  const rad = Math.max(0, Math.min(radius, Math.min(shape.w, shape.h) / 2));
  const radii = (v: number) => [corners & TL ? v : 0, corners & TR ? v : 0, corners & BR ? v : 0, corners & BL ? v : 0];
  ctx.beginPath();
  ctx.roundRect(shape.x, shape.y, shape.w, shape.h, radii(rad));
  const inner = inset(shape, width);
  if (inner.w > 0 && inner.h > 0) {
    ctx.roundRect(inner.x, inner.y, inner.w, inner.h, radii(Math.max(0, rad - width)));
  }
  ctx.fillStyle = color;
  ctx.fill('evenodd');
  ctx.restore();
};

export const drawShadow = (ctx: Ctx, r: Rect, radius: number, spread: number, color: string, alpha: number) => {
  const layers = Math.max(1, Math.floor(spread));
  for (let layer = layers; layer >= 1; layer--) {
    fillRound(ctx, {x: r.x - layer * 0.5, y: r.y - layer * 0.3, w: r.w + layer, h: r.h + layer}, radius + layer * 0.5, rgba(color, alpha / (layers + 1)));
  }
};

export const drawDots = (ctx: Ctx, y: number, left = 18, right = 382) => {
  ctx.fillStyle = C.Dot;
  ctx.beginPath();
  for (let x = left; x <= right; x += 8) {
    ctx.moveTo(x + 1.4, y);
    ctx.arc(x, y, 1.4, 0, Math.PI * 2);
  }
  ctx.fill();
};

// --- Backgrounds ----------------------------------------------------------------------

export const topBackground = (ctx: Ctx) => {
  ctx.fillStyle = gradient(ctx, R(0, 0, TOP_W, SCREEN_H), [
    [0, C.TopA],
    [1, C.TopB],
  ]);
  ctx.fillRect(0, 0, TOP_W, SCREEN_H);
};

export const bottomBackground = (ctx: Ctx) => {
  ctx.fillStyle = gradient(ctx, R(0, 0, BOTTOM_W, SCREEN_H), [
    [0, C.BottomA],
    [1, C.BottomB],
  ]);
  ctx.fillRect(0, 0, BOTTOM_W, SCREEN_H);
};

// --- Status bar -------------------------------------------------------------------------

export const statusBar = (ctx: Ctx, clock: string) => {
  for (let bar = 0; bar < 4; bar++) {
    const h = 4 + bar * 3;
    fillRect(ctx, 3 + bar * 5, 16 - h, 4, h, '#28B6F2');
  }
  const pill = R(27, 2, 106, 15);
  fillRound(ctx, pill, 5, '#31AAE0');
  fillRound(ctx, inset(pill, 1), 4, [
    [0, '#A8DEF7'],
    [0.5, '#63C4F1'],
    [1, '#2DA8DE'],
  ]);
  drawCentered(ctx, 'Connected', pill, 0.32, '#FFFFFF');
  drawText(ctx, clock, 364, 1, 0.4, '#323232', 'right');
  const battery = R(371, 3, 24, 13);
  fillRound(ctx, battery, 2.5, '#222222');
  fillRound(ctx, inset(battery, 1.5), 1.5, '#FFFFFF');
  fillRect(ctx, battery.x + battery.w, battery.y + 4, 2, 5, '#222222');
  for (let cell = 0; cell < 4; cell++) {
    fillRect(ctx, battery.x + 3 + cell * 4.6, battery.y + 3, 3.8, 7, '#4489DD');
  }
};

// The status bar's clock, "10/20 (Tue)  20:07", for seconds since midnight.
export const clockText = (seconds: number) => {
  const minutes = Math.floor(seconds / 60);
  const h = Math.floor(minutes / 60) % 24;
  const m = minutes % 60;
  return `10/20 (Tue)  ${h}:${String(m).padStart(2, '0')}`;
};

export const topChrome = (ctx: Ctx, clock: string, title: string, icon?: IconName | null, iconColor: string = C.Accent, since = 0) => {
  topBackground(ctx);
  statusBar(ctx, clock);
  const textW = textWidth(title, T.Title);
  const iconW = icon ? 28 : 0;
  const maxW = 360 - iconW;
  const width = iconW + Math.min(textW, maxW);
  let x = 200 - width / 2;
  if (icon) {
    drawIcon(ctx, icon, x, 26, 22, iconColor);
    x += iconW;
  }
  drawMarquee(ctx, title, x, 37 - lineHeight(T.Title) / 2 + 1, T.Title, C.Accent, maxW, 'left', since);
  drawDots(ctx, 57);
};

export const TOP_PANEL = R(17, 69, 366, 156);

export const panel = (ctx: Ctx, r: Rect = TOP_PANEL) => fillRound(ctx, r, 5, C.Panel);

export const recess = (ctx: Ctx, r: Rect) =>
  fillRound(ctx, r, 8, [
    [0, '#C9C08A'],
    [0.04, C.Recess],
    [1, C.Recess],
  ]);

export const panelText = (ctx: Ctx, value: string, r: Rect, s: number = T.Body, color: string = C.Ink, maxLines = 5) => {
  const lines = wrap(value, s, r.w - 32, maxLines);
  const step = lineHeight(s) * 1.05;
  let y = r.y + (r.h - step * lines.length) / 2;
  for (const line of lines) {
    drawText(ctx, line, r.x + r.w / 2, y, s, color, 'center');
    y += step;
  }
  return y;
};

// --- Buttons ---------------------------------------------------------------------------

export type Style = 'normal' | 'chosen' | 'current' | 'danger' | 'disabled';

type Look = {body: Stop[]; edgeTop: string; edgeBottom: string; highlight: string; label: string};

const lookFor = (style: Style): Look => {
  switch (style) {
    case 'chosen':
      return {
        body: [
          [0, '#FFF6B8'],
          [0.4, '#FFED7F'],
          [0.8, '#E4D766'],
          [1, '#C9BF49'],
        ],
        edgeTop: '#C8A23A',
        edgeBottom: '#A88A2A',
        highlight: '#FFFBD9',
        label: '#E07A10',
      };
    case 'current':
      return {
        body: [
          [0, '#9CC8D6'],
          [0.3, '#80B6C8'],
          [0.75, '#6C9DAB'],
          [1, '#557E86'],
        ],
        edgeTop: '#4E7A86',
        edgeBottom: '#3D626B',
        highlight: '#C9E6EF',
        label: '#FFFFFF',
      };
    case 'danger':
      return {
        body: [
          [0, '#FF6A4E'],
          [0.45, '#FE3014'],
          [0.7, '#D8250F'],
          [1, '#B31B0B'],
        ],
        edgeTop: '#9A1A0C',
        edgeBottom: '#7A1206',
        highlight: '#FFB2A0',
        label: '#FFFFFF',
      };
    default:
      return {
        body: [
          [0, '#FEFCF5'],
          [0.45, '#F4F1E6'],
          [0.78, '#E0DCCB'],
          [0.92, '#CFC9B0'],
          [1, '#B6B192'],
        ],
        edgeTop: '#B3AA83',
        edgeBottom: '#8F8659',
        highlight: '#FFFFFF',
        label: C.InkButton,
      };
  }
};

export const buttonBody = (ctx: Ctx, area: Rect, radius: number, style: Style, pressed: boolean) => {
  if (style === 'disabled') {
    strokeRound(ctx, area, radius, 1.5, '#D3C88F');
    return;
  }
  const look = lookFor(style);
  let body = area;
  let stops = look.body;
  if (pressed) {
    body = {...area, y: area.y + 1};
    stops = stops.map(([at, color]) => [at, darker(color, 0.1)] as Stop);
  } else {
    drawShadow(ctx, {...area, y: area.y + 2.5}, radius, 3, '#504620', 0x40 / 255);
  }
  fillRound(ctx, {x: body.x - 1, y: body.y - 1, w: body.w + 2, h: body.h + 2.5}, radius + 1, [
    [0, look.edgeTop],
    [0.8, look.edgeTop],
    [1, look.edgeBottom],
  ]);
  fillRound(ctx, body, radius, stops);
  fillRect(ctx, body.x + radius * 0.7, body.y + 0.6, body.w - radius * 1.4, 1, rgba(look.highlight, 0.85));
  if (style === 'chosen') {
    // Orange brackets at both ends, as on the system's option buttons.
    const outer = {x: body.x - 2, y: body.y - 2, w: body.w + 4, h: body.h + 4};
    const r = radius + 2;
    for (const left of [true, false]) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(left ? outer.x - 1 : outer.x + outer.w - r - 3, outer.y - 1, r + 4, outer.h + 2);
      ctx.clip();
      strokeRound(ctx, outer, r, 4, '#E89436');
      ctx.restore();
    }
  }
};

// A label that fits the button: smaller, or on two lines, before it is cut.
const buttonLabel = (ctx: Ctx, label: string, area: Rect, s: number, color: string) => {
  let use = s;
  if (textWidth(label, use) > area.w - 14) {
    use = Math.max(s * 0.82, (area.w - 14) / Math.max(1, textWidth(label, 1)));
  }
  if (textWidth(label, use) <= area.w - 14) {
    drawCentered(ctx, label, area, use, color);
    return;
  }
  const small = s * 0.82;
  const lines = wrap(label, small, area.w - 12, 2);
  const step = lineHeight(small) * 0.92;
  let y = area.y + area.h / 2 - (step * lines.length) / 2;
  for (const line of lines) {
    drawText(ctx, line, area.x + area.w / 2, y, small, color, 'center');
    y += step;
  }
};

export const buttonRadius = (h: number) => (h >= 34 ? 9 : h >= 28 ? 8 : 7);

export type ButtonOpts = {
  style?: Style;
  scale?: number;
  icon?: IconName | null;
  iconColor?: string;
  pressed?: boolean;
  labelColor?: string;
};

export const button = (ctx: Ctx, area: Rect, label: string, opts: ButtonOpts = {}) => {
  const {style = 'normal', scale = T.Button, icon, iconColor = C.IconGrey, pressed = false} = opts;
  const radius = buttonRadius(area.h);
  buttonBody(ctx, area, radius, style, pressed);
  let color = opts.labelColor ?? lookFor(style).label;
  if (style === 'disabled') color = '#CFC48A';
  const inside = pressed ? {...area, y: area.y + 1} : area;
  if (!icon) {
    buttonLabel(ctx, label, inside, scale, color);
    return;
  }
  const iconSize = Math.min(20, area.h - 10);
  const width = iconSize + 6 + textWidth(label, scale);
  const x = area.x + Math.max(8, (area.w - width) / 2);
  drawIcon(ctx, icon, x, inside.y + (area.h - iconSize) / 2, iconSize, iconColor);
  drawCenteredMarquee(ctx, label, {x: x + iconSize + 6, y: inside.y, w: area.x + area.w - 10 - (x + iconSize + 6), h: area.h}, scale, color, true);
};

export const tile = (ctx: Ctx, area: Rect, label: string, icon: IconName, iconColor: string, pressed = false) => {
  buttonBody(ctx, area, 10, 'normal', pressed);
  const lift = pressed ? 1 : 0;
  const s = 0.5;
  // Where the captures show the console breaking a label ("Scan / QR Code"),
  // the label carries the break: the console measures some text wider than
  // it draws, so its breaks cannot all be worked out from drawn widths.
  // A written-in break is kept whole: a line too wide for the stand-in font
  // shrinks to fit rather than losing its end to "...".
  const room = area.w - 14;
  const lines = label.includes('\n') ? label.split('\n') : wrap(label, s, room, 2);
  const step = lineHeight(s) * 0.88;
  const block = 30 + 3 + step * lines.length;
  let y = area.y + (area.h - block) / 2 + lift;
  drawIcon(ctx, icon, area.x + area.w / 2 - 15, y, 30, iconColor);
  y += 33;
  for (const line of lines) {
    const w = textWidth(line, s);
    const ls = w > room ? s * (room / w) : s;
    drawText(ctx, line, area.x + area.w / 2, y + (s - ls) * 6, ls, C.InkButton, 'center');
    y += step;
  }
};

// The D-pad ring around area, breathing gently (t in seconds).
export const focusRing = (ctx: Ctx, area: Rect, radius: number, t: number, gap = 3.5, width = 3) => {
  const pulse = 0.5 + 0.5 * Math.sin((((t * 1000) % 1200) / 1200) * Math.PI * 2);
  const glow = 0.55 + 0.45 * pulse;
  const ring = {x: area.x - gap, y: area.y - gap, w: area.w + 2 * gap, h: area.h + 2 * gap};
  strokeRound(ctx, {x: ring.x - 1, y: ring.y - 1, w: ring.w + 2, h: ring.h + 2}, radius + gap + 1, 1.5, rgba(C.FocusGlow, glow * 0.8));
  strokeRound(ctx, ring, radius + gap, width, C.Focus);
  strokeRound(ctx, inset(ring, width), Math.max(0, radius + gap - width), 1, rgba(C.FocusGlow, glow * 0.6));
};

// --- Tabs -------------------------------------------------------------------------------

export type TabSide = 'left' | 'right' | 'full';

export const tabRect = (side: TabSide): Rect =>
  side === 'left' ? R(0, 209, 126, 31) : side === 'right' ? R(194, 209, 126, 31) : R(0, 209, 320, 31);

const tabCorners = (side: TabSide) => (side === 'left' ? TR : side === 'right' ? TL : TOP);

export const tabRingRect = (side: TabSide) => {
  const a = tabRect(side);
  return R(a.x + 3, a.y + 3, a.w - 6, a.h - 3);
};

export const tabRing = (ctx: Ctx, side: TabSide) => strokeRound(ctx, tabRingRect(side), 19, 3, C.Focus, tabCorners(side), true);

export const tab = (ctx: Ctx, side: TabSide, label: string, key = '', focused = false, pressed = false) => {
  const area = tabRect(side);
  const corners = tabCorners(side);
  fillRound(
    ctx,
    {x: area.x - (side === 'left' ? 0 : 2), y: area.y - 2, w: area.w + (side === 'full' ? 4 : 2), h: area.h + 2},
    24,
    'rgba(0,0,0,0.19)',
    corners,
  );
  fillRound(ctx, area, 22, '#C4C4C4', corners);
  let body: Stop[] = [
    [0, '#8A8A8A'],
    [0.12, '#7C7C7C'],
    [0.6, '#707070'],
    [1, '#666666'],
  ];
  if (pressed) body = body.map(([at, c]) => [at, darker(c, 0.2)] as Stop);
  fillRound(ctx, {x: area.x, y: area.y + 2, w: area.w, h: area.h - 2}, 21, body, corners);

  const keyWidth = key ? Math.max(15, textWidth(key, 0.36) + 7) + 5 : 0;
  let labelScale = T.Tab;
  while (labelScale > 0.45 && textWidth(label, labelScale) + keyWidth > area.w - 22) labelScale -= 0.02;
  const labelWidth = textWidth(label, labelScale);
  const ins = side === 'full' ? 0 : 6;
  const centre = side === 'right' ? area.x + ins + (area.w - ins) / 2 : area.x + (area.w - ins) / 2;
  let x = centre - (labelWidth + keyWidth) / 2;
  const line = R(x, area.y + 3.5, labelWidth + keyWidth, area.h - 3);
  if (key) {
    const capW = keyWidth - 5;
    const cap = R(x, line.y + (line.h - 15) / 2, capW, 15);
    fillRound(ctx, cap, key.length === 1 ? 7.5 : 4, '#FFFFFF');
    drawCentered(ctx, key, cap, 0.36, '#6C6C6C');
    x += keyWidth;
  }
  drawCentered(ctx, label, R(x, line.y, labelWidth, line.h), labelScale, '#FFFFFF', true);
  if (focused) tabRing(ctx, side);
};

// --- Tags, badges, progress, spinner ---------------------------------------------------------

export const tag = (ctx: Ctx, x: number, y: number, label: string, fill: string = C.Tag) => {
  const width = textWidth(label, 0.4) + 16;
  fillRound(ctx, R(x, y, width, 20), 5, fill);
  drawCentered(ctx, label, R(x, y, width, 20), 0.4, '#FFFFFF');
  return width;
};

export const badge = (ctx: Ctx, right: number, y: number, label: string, fill: string = C.Badge) => {
  const width = Math.max(34, textWidth(label, 0.34) + 10);
  fillRound(ctx, R(right - width, y, width, 16), 4, fill);
  drawCentered(ctx, label, R(right - width, y, width, 16), 0.34, '#FFFFFF');
  return width;
};

export const tagRow = (ctx: Ctx, x: number, y: number, tagWidth: number, label: string, value: string, fill: string = C.Tag, since = 0) => {
  fillRound(ctx, R(x, y, tagWidth, 20), 5, fill);
  drawCentered(ctx, fitEnd(label, 0.4, tagWidth - 6), R(x, y, tagWidth, 20), 0.4, '#FFFFFF');
  drawCenteredMarquee(ctx, value, R(x + tagWidth + 10, y, 366 - tagWidth - 40, 20), T.Small, C.Ink, true, since);
  return y + 26;
};

export const progressBar = (ctx: Ctx, area: Rect, fraction: number, t = 0) => {
  const radius = area.h / 2;
  fillRound(ctx, area, radius, [
    [0, '#B5AC77'],
    [0.3, '#CFC795'],
    [1, '#D6CF9F'],
  ]);
  const fill = (x: number, width: number) => {
    if (width < 1) return;
    const r = Math.min(radius, width / 2);
    fillRound(ctx, R(x, area.y, width, area.h), r, [
      [0, '#FFE27A'],
      [0.4, '#FFCC22'],
      [1, '#FF9900'],
    ]);
    fillRect(ctx, x + r, area.y + 1.5, Math.max(0, width - 2 * r), 1.5, 'rgba(255,255,255,0.5)');
  };
  if (fraction < 0) {
    const width = area.w * 0.28;
    const phase = ((t * 1000) % 1400) / 1400;
    const x = area.x + (area.w + width) * phase - width;
    const left = Math.max(area.x, x);
    const right = Math.min(area.x + area.w, x + width);
    fill(left, right - left);
    return;
  }
  fill(area.x, area.w * Math.min(1, fraction));
};

export const spinner = (ctx: Ctx, cx: number, cy: number, radius: number, t: number, color: string = C.Ink) => {
  const lead = Math.floor(t * 10) % 8;
  for (let i = 0; i < 8; i++) {
    const angle = (i * Math.PI) / 4 - Math.PI / 2;
    const age = (lead - i + 8) % 8;
    const c = Math.cos(angle);
    const s = Math.sin(angle);
    const inner = radius * 0.42;
    const half = radius * 0.13;
    ctx.beginPath();
    ctx.moveTo(cx + c * inner - s * half, cy + s * inner + c * half);
    ctx.lineTo(cx + c * radius - s * half, cy + s * radius + c * half);
    ctx.lineTo(cx + c * radius + s * half, cy + s * radius - c * half);
    ctx.lineTo(cx + c * inner + s * half, cy + s * inner - c * half);
    ctx.closePath();
    ctx.fillStyle = rgba(color, 1 - age * 0.11);
    ctx.fill();
  }
};

// --- Dialogs ---------------------------------------------------------------------------------

export const DIALOG_BOX = R(10, 12, 300, 216);
export const STRIP_H = 39;
export const DANGER_STRIP_H = 30;

export const dialogBox = (ctx: Ctx, box: Rect) => {
  for (let ring = 0; ring < 6; ring++) {
    fillRound(ctx, {x: box.x - ring * 1.6, y: box.y - ring * 1.6 + 1, w: box.w + ring * 3.2, h: box.h + ring * 3.2}, 13 + ring * 1.6, 'rgba(0,0,0,0.07)');
  }
  fillRound(ctx, box, 13, [
    [0, '#FFFDF0'],
    [0.12, '#F6F4EB'],
    [0.5, '#E6E5DE'],
    [0.88, '#F2F0E6'],
    [1, '#FFFDF0'],
  ]);
};

export const stripButtons = (box: Rect, count: number, height: number) => {
  const width = box.w / Math.max(1, count);
  return Array.from({length: count}, (_, i) => R(box.x + i * width, box.y + box.h - height, width, height));
};

export const strip = (
  ctx: Ctx,
  box: Rect,
  labels: string[],
  selected: number,
  pressed: number,
  dark: boolean,
  height: number,
  dangerLabel: number,
) => {
  const rects = stripButtons(box, labels.length, height);
  rects.forEach((area, index) => {
    let corners = 0;
    if (index === 0) corners |= BL;
    if (index === rects.length - 1) corners |= BR;
    let stops: Stop[] = dark
      ? [
          [0, '#B9B7B4'],
          [0.2, '#9D9B98'],
          [0.7, '#57585A'],
          [1, '#333435'],
        ]
      : [
          [0, '#FFFDF2'],
          [0.45, '#FFF0B4'],
          [0.8, '#FAE29A'],
          [1, '#AE8D67'],
        ];
    if (index === pressed) stops = stops.map(([at, c]) => [at, darker(c, 0.12)] as Stop);
    fillRound(ctx, area, 13, stops, corners);
    if (index > 0) fillRect(ctx, area.x, area.y, 1, area.h, '#191817');
    const color = dark ? '#FFFFFF' : index === dangerLabel ? C.Bad : C.InkDialog;
    let s = T.Tab;
    // The app stops shrinking at 0.42 and lets a long label run over; here
    // it keeps shrinking so a label never spills onto its neighbour.
    while (s > 0.3 && textWidth(labels[index], s) > area.w - 12) s -= 0.02;
    drawCentered(ctx, labels[index], area, s, color);
    if (index === selected) strokeRound(ctx, inset(area, 3), 6, 3, C.Focus, corners);
  });
  fillRect(ctx, box.x, box.y + box.h - height, box.w, 1, '#555553');
};

// --- Notifications ---------------------------------------------------------------------------

export type Notice = {title: string; text: string; icon: IconName; color: string};

const NOTICE_IN = 0.32;
const NOTICE_HOLD = 6;
const NOTICE_OUT = 0.26;

const easeBack = (v: number) => {
  const c = 1.4;
  const u = v - 1;
  return 1 + (c + 1) * u * u * u + c * u * u;
};

// The HOME Menu style banner on the top screen, age seconds after it came.
export const notice = (ctx: Ctx, n: Notice, age: number) => {
  if (age < 0 || age >= NOTICE_IN + NOTICE_HOLD + NOTICE_OUT) return;
  let shown = 1;
  if (age < NOTICE_IN) shown = easeBack(age / NOTICE_IN);
  else if (age > NOTICE_IN + NOTICE_HOLD) {
    const v = (age - NOTICE_IN - NOTICE_HOLD) / NOTICE_OUT;
    shown = 1 - v * v;
  }
  const W = 336;
  const H = 50;
  const x = (TOP_W - W) / 2;
  const y = -H - 8 + (22 + H + 8) * shown;
  const card = R(x, y, W, H);
  drawShadow(ctx, R(x, y + 2, W, H), 12, 4, '#403810', 0x48 / 255);
  fillRound(ctx, card, 12, '#C8BF96');
  fillRound(ctx, inset(card, 1), 11, [
    [0, '#FFFFFF'],
    [0.6, '#FAF8EF'],
    [1, '#EFEBDA'],
  ]);
  const b = R(x + 8, y + 7, 36, 36);
  fillRound(ctx, b, 9, n.color);
  fillRound(ctx, R(b.x, b.y, b.w, b.h / 2), 9, 'rgba(255,255,255,0.22)', TOP);
  drawIcon(ctx, n.icon, b.x + 6, b.y + 6, 24, '#FFFFFF');
  const textX = b.x + b.w + 10;
  const maxW = x + W - 12 - textX;
  drawMarquee(ctx, n.title, textX, y + 6, T.Small, C.InkStrong, maxW);
  drawMarquee(ctx, n.text, textX, y + 27, T.Tiny, C.Ink, maxW);
  if (age >= NOTICE_IN && age <= NOTICE_IN + NOTICE_HOLD) {
    const left = 1 - (age - NOTICE_IN) / NOTICE_HOLD;
    fillRect(ctx, textX, y + H - 5, maxW * left, 2, rgba(n.color, 0.55));
  }
};

// --- QR codes as the app draws them (qr_code.cpp) --------------------------------------------

type Code = {size: number; dark: (x: number, y: number) => boolean};
const codes = new Map<string, Code>();

// Medium error correction, raised while the code stays the same size, as
// qrcodegen's boostEcl does; compact codes start from low.
export const appQr = (text: string, compact = false): Code => {
  const key = (compact ? 'L|' : 'M|') + text;
  const hit = codes.get(key);
  if (hit) return hit;
  const base = QRCode.create(text, {errorCorrectionLevel: compact ? 'L' : 'M'});
  let best = base;
  for (const level of ['H', 'Q', 'M'] as const) {
    const tryIt = QRCode.create(text, {errorCorrectionLevel: level});
    if (tryIt.version === base.version) {
      best = tryIt;
      break;
    }
  }
  const size = best.modules.size;
  const data = best.modules.data;
  const code = {size, dark: (x: number, y: number) => data[y * size + x] === 1};
  codes.set(key, code);
  return code;
};

export const drawAppQr = (ctx: Ctx, code: Code, x: number, y: number, px: number) => {
  const quiet = 2 * px;
  fillRound(ctx, R(x - quiet, y - quiet, code.size * px + 2 * quiet, code.size * px + 2 * quiet), 4, '#FFFFFF');
  ctx.fillStyle = '#222222';
  ctx.beginPath();
  for (let row = 0; row < code.size; row++) {
    for (let col = 0; col < code.size; col++) {
      if (code.dark(col, row)) ctx.rect(x + col * px, y + row * px, px + 0.02, px + 0.02);
    }
  }
  ctx.fill();
};

// --- Title icons --------------------------------------------------------------------------------

export type IconPainter = (ctx: Ctx, x: number, y: number) => void;

// ui::drawTitleIcon: a 48 px title icon framed like the HOME Menu's, or a
// stand-in glyph.
export const titleIcon = (ctx: Ctx, x: number, y: number, painter: IconPainter | null, fallback: IconName = 'Package') => {
  drawShadow(ctx, R(x, y + 1.5, 48, 48), 8, 2, '#403810', 0x50 / 255);
  fillRound(ctx, R(x - 1, y - 1, 50, 50), 9, '#B3AA83');
  if (painter) {
    fillRound(ctx, R(x, y, 48, 48), 8, '#FFFFFF');
    painter(ctx, x, y);
    return;
  }
  fillRound(ctx, R(x, y, 48, 48), 8, [
    [0, '#4A4F59'],
    [1, '#1E2127'],
  ]);
  drawIcon(ctx, fallback, x + 9, y + 9, 30, '#8FD1FF');
};

// Files' icon for a kind of file: a cream tile with the kind's icon.
export const kindTile = (ctx: Ctx, x: number, y: number, icon: IconName, color: string) => {
  fillRound(ctx, R(x - 1, y - 1, 50, 50), 9, '#C9C08A');
  fillRound(ctx, R(x, y, 48, 48), 8, [
    [0, '#FEFCF5'],
    [1, '#E6E2D0'],
  ]);
  drawIcon(ctx, icon, x + 9, y + 9, 30, color);
};

export {drawCentered, drawCenteredMarquee, drawMarquee, drawText, drawWrapped, centeredY, lineHeight, textWidth, wrap, fitEnd};
