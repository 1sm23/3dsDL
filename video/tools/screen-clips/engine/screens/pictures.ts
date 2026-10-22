import {staticFile} from 'remotion';
import {drawMonitorPage, MONITOR_PAGE} from '../lib/monitorPage';
import icon3dsfmUrl from './assets/3dsfm-icon.png';
import iconSh23dsUrl from './assets/sh23ds-icon.png';
import {drawIcon} from './icons';
import {drawText, getDeviceScale} from './text';
import {Ctx, prng, R, Rect, TOP_W, SCREEN_H} from './theme';
import {fillRound, IconPainter} from './draw';

// Pictures: title icons, the camera's view of the desk, and the two photos
// in the picture viewer. All original, drawn here.

// --- Loaded images ------------------------------------------------------------------------

const images: Record<string, HTMLImageElement> = {};

const load = (key: string, src: string) =>
  new Promise<void>((resolve, reject) => {
    const img = new Image();
    img.onload = () => {
      images[key] = img;
      resolve();
    };
    img.onerror = () => reject(new Error(`could not load ${src}`));
    img.src = src;
  });

export const loadPictures = () =>
  Promise.all([load('3dsfm', icon3dsfmUrl), load('sh23ds', iconSh23dsUrl), load('3dsdl', staticFile('brand/icon-256.png'))]).then(() => undefined);

const imageIcon =
  (key: string): IconPainter =>
  (ctx, x, y) => {
    const img = images[key];
    if (!img) return;
    ctx.save();
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(img, x, y, 48, 48);
    ctx.restore();
  };

// --- Title icons ------------------------------------------------------------------------------

const square = (ctx: Ctx, x: number, y: number, top: string, bottom: string) => {
  const g = ctx.createLinearGradient(0, y, 0, y + 48);
  g.addColorStop(0, top);
  g.addColorStop(1, bottom);
  ctx.fillStyle = g;
  ctx.fillRect(x, y, 48, 48);
};

const shine = (ctx: Ctx, x: number, y: number) => {
  const g = ctx.createLinearGradient(0, y, 0, y + 24);
  g.addColorStop(0, 'rgba(255,255,255,0.28)');
  g.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = g;
  ctx.fillRect(x, y, 48, 24);
};

export const ICONS: Record<string, IconPainter> = {
  '3dsFM': imageIcon('3dsfm'),
  sh23ds: imageIcon('sh23ds'),
  '3dsDL': imageIcon('3dsdl'),
  Checkpoint: (ctx, x, y) => {
    square(ctx, x, y, '#2B3A4A', '#141C25');
    ctx.save();
    ctx.strokeStyle = '#7FD8C0';
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.arc(x + 24, y + 24, 14, 0, Math.PI * 2);
    ctx.stroke();
    ctx.restore();
    drawIcon(ctx, 'Check', x + 13, y + 13, 22, '#FFFFFF');
    shine(ctx, x, y);
  },
  Anemone3DS: (ctx, x, y) => {
    square(ctx, x, y, '#F7A8C8', '#C25A92');
    ctx.save();
    ctx.translate(x + 24, y + 25);
    ctx.fillStyle = '#FFF4F9';
    for (let i = 0; i < 5; i++) {
      ctx.rotate((Math.PI * 2) / 5);
      ctx.beginPath();
      ctx.ellipse(0, -9, 6, 9.5, 0, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.fillStyle = '#F2B632';
    ctx.beginPath();
    ctx.arc(0, 0, 5, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
    shine(ctx, x, y);
  },
  'Universal-Updater': (ctx, x, y) => {
    square(ctx, x, y, '#4FB3F0', '#1F6FB8');
    drawIcon(ctx, 'Download', x + 8, y + 8, 32, '#FFFFFF');
    shine(ctx, x, y);
  },
  ftpd: (ctx, x, y) => {
    square(ctx, x, y, '#3D8F5A', '#1D5233');
    drawIcon(ctx, 'Globe', x + 9, y + 9, 30, '#E8FFF0');
    shine(ctx, x, y);
  },
  GodMode9: (ctx, x, y) => {
    square(ctx, x, y, '#3A2F1E', '#120E08');
    drawIcon(ctx, 'Chip', x + 9, y + 9, 30, '#E9B949');
    shine(ctx, x, y);
  },
  'Starfall Racers': (ctx, x, y) => {
    square(ctx, x, y, '#3B2C7A', '#120C33');
    ctx.save();
    ctx.beginPath();
    ctx.rect(x, y, 48, 48);
    ctx.clip();
    const rnd = prng(7);
    ctx.fillStyle = '#FFFFFF';
    for (let i = 0; i < 14; i++) ctx.fillRect(x + rnd() * 48, y + rnd() * 30, 1, 1);
    const g = ctx.createLinearGradient(x + 6, y + 34, x + 34, y + 12);
    g.addColorStop(0, 'rgba(255,190,90,0)');
    g.addColorStop(1, '#FFD45A');
    ctx.strokeStyle = g;
    ctx.lineWidth = 3;
    ctx.lineCap = 'round';
    ctx.beginPath();
    ctx.moveTo(x + 6, y + 34);
    ctx.lineTo(x + 34, y + 13);
    ctx.stroke();
    ctx.fillStyle = '#FFF2B0';
    star(ctx, x + 35, y + 12, 6);
    ctx.fillStyle = '#E2462F';
    ctx.beginPath();
    ctx.roundRect(x + 10, y + 36, 28, 7, 3);
    ctx.fill();
    ctx.fillStyle = '#1B1B22';
    ctx.beginPath();
    ctx.arc(x + 16, y + 44, 3, 0, Math.PI * 2);
    ctx.arc(x + 32, y + 44, 3, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
    shine(ctx, x, y);
  },
  'Pocket Orchard': (ctx, x, y) => {
    square(ctx, x, y, '#9FDBF5', '#6CC06A');
    ctx.save();
    ctx.fillStyle = '#4E9E45';
    ctx.fillRect(x, y + 34, 48, 14);
    ctx.fillStyle = '#E2402F';
    ctx.beginPath();
    ctx.arc(x + 20, y + 26, 10, 0, Math.PI * 2);
    ctx.arc(x + 28, y + 26, 10, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = 'rgba(255,255,255,0.45)';
    ctx.beginPath();
    ctx.ellipse(x + 18, y + 21, 3, 4.5, -0.5, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = '#6B4425';
    ctx.fillRect(x + 23, y + 10, 2.4, 8);
    ctx.fillStyle = '#3DA144';
    ctx.beginPath();
    ctx.ellipse(x + 30, y + 13, 6, 3, -0.4, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
    shine(ctx, x, y);
  },
};

function star(ctx: Ctx, cx: number, cy: number, r: number) {
  ctx.beginPath();
  for (let i = 0; i < 10; i++) {
    const a = -Math.PI / 2 + (i * Math.PI) / 5;
    const rr = i % 2 === 0 ? r : r * 0.45;
    ctx.lineTo(cx + Math.cos(a) * rr, cy + Math.sin(a) * rr);
  }
  ctx.closePath();
  ctx.fill();
}

// --- Cached painted pictures --------------------------------------------------------------------

const cache = new Map<string, OffscreenCanvas>();

// Paints a w x h picture once per device scale and keeps it.
const cached = (key: string, w: number, h: number, paint: (ctx: Ctx) => void) => {
  const scale = getDeviceScale();
  const k = `${key}|${scale}`;
  const hit = cache.get(k);
  if (hit) return hit;
  const canvas = new OffscreenCanvas(Math.ceil(w * scale), Math.ceil(h * scale));
  const ctx = canvas.getContext('2d') as unknown as Ctx;
  ctx.setTransform(scale, 0, 0, scale, 0, 0);
  paint(ctx);
  cache.set(k, canvas);
  return canvas;
};

const blit = (ctx: Ctx, canvas: OffscreenCanvas, x: number, y: number, w: number, h: number) => ctx.drawImage(canvas, x, y, w, h);

// --- Photo 1: a sunset over hills and a lake ---------------------------------------------------------

const paintSunset = (ctx: Ctx) => {
  const W = TOP_W;
  const H = SCREEN_H;
  const horizon = 150;
  const sky = ctx.createLinearGradient(0, 0, 0, horizon);
  sky.addColorStop(0, '#2E3A78');
  sky.addColorStop(0.35, '#8A4E8C');
  sky.addColorStop(0.62, '#E5735A');
  sky.addColorStop(0.85, '#F9B25B');
  sky.addColorStop(1, '#FCD58A');
  ctx.fillStyle = sky;
  ctx.fillRect(0, 0, W, horizon);
  // The sun, low, with its glow.
  const sx = 238;
  const sy = 128;
  const glow = ctx.createRadialGradient(sx, sy, 4, sx, sy, 150);
  glow.addColorStop(0, 'rgba(255,240,190,0.95)');
  glow.addColorStop(0.15, 'rgba(255,214,140,0.6)');
  glow.addColorStop(1, 'rgba(255,170,100,0)');
  ctx.fillStyle = glow;
  ctx.fillRect(0, 0, W, horizon);
  ctx.fillStyle = '#FFF4D2';
  ctx.beginPath();
  ctx.arc(sx, sy, 17, 0, Math.PI * 2);
  ctx.fill();
  // Thin clouds catching the light.
  const rnd = prng(11);
  for (let i = 0; i < 9; i++) {
    const cy = 30 + rnd() * 70;
    const cx = rnd() * W;
    const w = 60 + rnd() * 110;
    const g = ctx.createLinearGradient(cx - w / 2, 0, cx + w / 2, 0);
    const a = 0.18 + rnd() * 0.25;
    g.addColorStop(0, 'rgba(255,190,170,0)');
    g.addColorStop(0.5, `rgba(255,${170 + Math.floor(rnd() * 40)},150,${a})`);
    g.addColorStop(1, 'rgba(255,190,170,0)');
    ctx.fillStyle = g;
    ctx.beginPath();
    ctx.ellipse(cx, cy, w / 2, 2.5 + rnd() * 3, 0, 0, Math.PI * 2);
    ctx.fill();
  }
  // Hills in layers, hazier further away.
  const hill = (base: number, amp: number, seed: number, color: string) => {
    const r = prng(seed);
    const phases = [r() * 6, r() * 6, r() * 6];
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.moveTo(0, horizon + 1);
    for (let x = 0; x <= W; x += 4) {
      const y = base - amp * (0.55 * Math.sin(x / 61 + phases[0]) + 0.3 * Math.sin(x / 23 + phases[1]) + 0.15 * Math.sin(x / 9 + phases[2]));
      ctx.lineTo(x, y);
    }
    ctx.lineTo(W, horizon + 1);
    ctx.closePath();
    ctx.fill();
  };
  hill(126, 16, 3, '#B5627A');
  hill(136, 18, 5, '#7A3F6B');
  hill(146, 12, 9, '#4A2A55');
  // The lake mirrors the sky, darker, with ripples of light.
  const lake = ctx.createLinearGradient(0, horizon, 0, H);
  lake.addColorStop(0, '#E9A06A');
  lake.addColorStop(0.25, '#9A5878');
  lake.addColorStop(1, '#2A2450');
  ctx.fillStyle = lake;
  ctx.fillRect(0, horizon, W, H - horizon);
  const r2 = prng(23);
  for (let i = 0; i < 60; i++) {
    const y = horizon + 3 + Math.pow(r2(), 1.6) * (H - horizon - 6);
    const near = (y - horizon) / (H - horizon);
    const w = 6 + r2() * 30 * (1 - near * 0.5);
    const x = sx + (r2() - 0.5) * (30 + near * 160);
    ctx.fillStyle = `rgba(255,226,170,${0.5 - near * 0.4})`;
    ctx.fillRect(x - w / 2, y, w, 1 + near);
  }
  // A shore in the front.
  ctx.fillStyle = '#1C1830';
  ctx.beginPath();
  ctx.moveTo(0, 214);
  ctx.quadraticCurveTo(90, 204, 170, 222);
  ctx.quadraticCurveTo(220, 232, 260, 240);
  ctx.lineTo(0, 240);
  ctx.closePath();
  ctx.fill();
  // Reeds.
  ctx.strokeStyle = '#151226';
  ctx.lineWidth = 1.2;
  for (let i = 0; i < 26; i++) {
    const x = 6 + i * 6 + r2() * 4;
    const top = 168 + r2() * 30;
    ctx.beginPath();
    ctx.moveTo(x, 222);
    ctx.quadraticCurveTo(x + 2, (top + 222) / 2, x + 4 + r2() * 3, top);
    ctx.stroke();
  }
};

// --- Photo 2: a night sky with a blue aurora ----------------------------------------------------------

const paintAurora = (ctx: Ctx) => {
  const W = TOP_W;
  const H = SCREEN_H;
  const sky = ctx.createLinearGradient(0, 0, 0, H);
  sky.addColorStop(0, '#040A1C');
  sky.addColorStop(0.55, '#0B1F3E');
  sky.addColorStop(1, '#123456');
  ctx.fillStyle = sky;
  ctx.fillRect(0, 0, W, H);
  const rnd = prng(42);
  for (let i = 0; i < 260; i++) {
    const x = rnd() * W;
    const y = rnd() * 170;
    const b = rnd();
    ctx.fillStyle = `rgba(255,255,255,${0.25 + b * 0.7})`;
    const s = b > 0.93 ? 1.4 : 0.7;
    ctx.fillRect(x, y, s, s);
  }
  // Curtains of light in 3dsDL blue and SD card cyan, rising from a curve.
  ctx.save();
  ctx.globalCompositeOperation = 'lighter';
  const ribbon = (seed: number, baseY: number, height: number, color: [number, number, number], alpha: number) => {
    const r = prng(seed);
    const p1 = r() * 6;
    const p2 = r() * 6;
    for (let x = -10; x <= W + 10; x += 1.5) {
      const curve = baseY + 22 * Math.sin(x / 70 + p1) + 9 * Math.sin(x / 27 + p2);
      const h = height * (0.55 + 0.45 * Math.sin(x / 41 + p2 * 2) ** 2);
      const g = ctx.createLinearGradient(0, curve - h, 0, curve);
      g.addColorStop(0, `rgba(${color[0]},${color[1]},${color[2]},0)`);
      g.addColorStop(0.7, `rgba(${color[0]},${color[1]},${color[2]},${alpha * 0.6})`);
      g.addColorStop(1, `rgba(${color[0]},${color[1]},${color[2]},${alpha})`);
      ctx.fillStyle = g;
      ctx.fillRect(x, curve - h, 1.6, h + 3);
    }
  };
  ribbon(3, 118, 90, [28, 147, 194], 0.55);
  ribbon(8, 104, 70, [63, 210, 240], 0.45);
  ribbon(15, 132, 50, [120, 240, 200], 0.22);
  ctx.restore();
  // Mountains and the snow at their feet.
  const ridge = (base: number, amp: number, seed: number, color: string) => {
    const r = prng(seed);
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.moveTo(0, H);
    let y = base;
    for (let x = 0; x <= W; x += 5) {
      y += (r() - 0.5) * amp;
      y = Math.max(base - 30, Math.min(base + 16, y));
      ctx.lineTo(x, y);
    }
    ctx.lineTo(W, H);
    ctx.closePath();
    ctx.fill();
  };
  ridge(170, 9, 4, '#0D1B33');
  ridge(190, 7, 6, '#08111F');
  const snow = ctx.createLinearGradient(0, 214, 0, H);
  snow.addColorStop(0, '#2C4A6A');
  snow.addColorStop(1, '#183049');
  ctx.fillStyle = snow;
  ctx.beginPath();
  ctx.moveTo(0, 222);
  ctx.quadraticCurveTo(200, 206, 400, 220);
  ctx.lineTo(400, 240);
  ctx.lineTo(0, 240);
  ctx.closePath();
  ctx.fill();
  // Pines in silhouette.
  ctx.fillStyle = '#050A12';
  const r3 = prng(77);
  for (let i = 0; i < 14; i++) {
    const x = 20 + i * 28 + r3() * 14;
    const h = 18 + r3() * 22;
    const base = 228 - r3() * 8;
    ctx.beginPath();
    ctx.moveTo(x, base - h);
    ctx.lineTo(x - h * 0.28, base);
    ctx.lineTo(x + h * 0.28, base);
    ctx.closePath();
    ctx.fill();
  }
};

export const drawPhoto = (ctx: Ctx, which: 'sunset' | 'aurora') => {
  const canvas = cached(which, TOP_W, SCREEN_H, which === 'sunset' ? paintSunset : paintAurora);
  blit(ctx, canvas, 0, 0, TOP_W, SCREEN_H);
};

// --- The camera: a monitor on a desk showing the QR code ---------------------------------------------

type Pt = {x: number; y: number};

const noise: OffscreenCanvas[] = [];
const noiseFrame = (index: number) => {
  if (!noise.length) {
    for (let n = 0; n < 4; n++) {
      const c = new OffscreenCanvas(400, 240);
      const x = c.getContext('2d')!;
      const img = x.createImageData(400, 240);
      const r = prng(100 + n);
      for (let i = 0; i < img.data.length; i += 4) {
        const v = Math.floor(r() * 255);
        img.data[i] = v;
        img.data[i + 1] = v;
        img.data[i + 2] = v;
        img.data[i + 3] = 255;
      }
      x.putImageData(img, 0, 0);
      noise.push(c);
    }
  }
  return noise[index % noise.length];
};

// The desk monitor's page (src/lib/monitorPage.ts), drawn once the sh23ds
// icon has loaded.
let pageCanvas: HTMLCanvasElement | null = null;
const monitorPage = () => {
  if (pageCanvas) return pageCanvas;
  const c = document.createElement('canvas');
  c.width = MONITOR_PAGE.w;
  c.height = MONITOR_PAGE.h;
  const icon = images['sh23ds'] ?? null;
  drawMonitorPage(c.getContext('2d')!, c.width, c.height, icon);
  if (icon) pageCanvas = c;
  return c;
};

let camLayer: OffscreenCanvas | null = null;

// The outer camera's picture at t seconds into the scan: the desk monitor
// and its page, a hand-held drift, the code moving to the middle.
// settle runs 0..1 as the code reaches the brackets.
export const drawCamera = (ctx: Ctx, t: number, settle: number) => {
  const scale = getDeviceScale();
  const W = TOP_W;
  const H = SCREEN_H;
  if (!camLayer || camLayer.width !== W * scale) camLayer = new OffscreenCanvas(W * scale, H * scale);
  const c = camLayer.getContext('2d') as unknown as Ctx;
  c.setTransform(scale, 0, 0, scale, 0, 0);

  // Hand-held: small sines, never quite still.
  const sway = {
    x: 3.2 * Math.sin(t * 1.3) + 1.6 * Math.sin(t * 2.9 + 1),
    y: 2.4 * Math.sin(t * 1.1 + 2) + 1.2 * Math.sin(t * 3.4),
    r: 0.012 * Math.sin(t * 0.9 + 0.5),
  };
  const e = settle * settle * (3 - 2 * settle);
  // Settled, the code sits in the middle of the brackets at (200, 116);
  // before that it is off to the right and low, as the console comes up.
  const offX = 70 * (1 - e) + sway.x;
  const offY = 34 * (1 - e) + sway.y;

  // The room as the desk scene has it: a cream wall, the wooden desk and the
  // potted plant to the left of the monitor, seen warmer and darker by a
  // small camera.
  const wall = c.createLinearGradient(0, 0, 0, H);
  wall.addColorStop(0, '#BDB09A');
  wall.addColorStop(1, '#A29481');
  c.fillStyle = wall;
  c.fillRect(0, 0, W, H);

  c.save();
  c.translate(W / 2, H / 2);
  c.rotate(sway.r);
  c.translate(-W / 2 + offX, -H / 2 + offY);

  // The monitor's screen, 16:9 and turned a touch away, close enough that
  // the code fills the brackets once settled.
  const SW = 352;
  const SH = 198;
  const skew = {x: 2, y: 6}; // the far edge sits a little lower and in
  const P = MONITOR_PAGE;
  const cu = (P.qr.x + P.qr.side / 2) / P.w;
  const cv = (P.qr.y + P.qr.side / 2) / P.h;
  const tl = {x: 200 - cu * SW - cv * skew.x, y: 116 - cu * skew.y - cv * SH};
  const at = (u: number, v: number): Pt => ({x: tl.x + u * SW + v * skew.x, y: tl.y + u * skew.y + v * SH});

  // Desk, far below the screen on its stand.
  const desk = c.createLinearGradient(0, tl.y + SH + 40, 0, tl.y + SH + 140);
  desk.addColorStop(0, '#B39066');
  desk.addColorStop(1, '#8C6A45');
  c.fillStyle = desk;
  c.fillRect(-300, tl.y + SH + 40, 1000, 300);
  // Stand: the white neck under the screen.
  const neck = at(0.5, 1);
  c.fillStyle = '#CFC9BD';
  c.fillRect(neck.x - 16, neck.y, 32, 60);
  // Bezel.
  const corners = [at(0, 0), at(1, 0), at(1, 1), at(0, 1)];
  const b = 9;
  c.fillStyle = '#1B1C20';
  c.beginPath();
  c.moveTo(corners[0].x - b, corners[0].y - b);
  c.lineTo(corners[1].x + b, corners[1].y - b);
  c.lineTo(corners[2].x + b, corners[2].y + b);
  c.lineTo(corners[3].x - b, corners[3].y + b);
  c.closePath();
  c.fill();
  // The page itself, the same one the monitor shows.
  c.save();
  c.transform(SW / P.w, skew.y / P.w, skew.x / P.h, SH / P.h, tl.x, tl.y);
  c.imageSmoothingQuality = 'high';
  c.drawImage(monitorPage(), 0, 0);
  c.restore();
  // The screen's own glow and a reflection across it.
  c.save();
  c.beginPath();
  c.moveTo(corners[0].x, corners[0].y);
  for (const q of corners.slice(1)) c.lineTo(q.x, q.y);
  c.closePath();
  c.clip();
  const sheen = c.createLinearGradient(corners[0].x, corners[0].y, corners[2].x, corners[2].y);
  sheen.addColorStop(0, 'rgba(255,255,255,0.10)');
  sheen.addColorStop(0.5, 'rgba(255,255,255,0)');
  sheen.addColorStop(1, 'rgba(255,240,220,0.06)');
  c.fillStyle = sheen;
  c.fillRect(-300, -300, 1000, 800);
  c.restore();

  // The plant's leaves at the left edge, soft and out of focus.
  c.save();
  c.filter = `blur(${2 * scale}px)`;
  for (const [x, y, rx, ry, rot, col] of [
    [tl.x - 70, tl.y + 40, 34, 16, -0.6, '#3F5B2F'],
    [tl.x - 52, tl.y + 92, 40, 18, 0.35, '#4E6D37'],
    [tl.x - 86, tl.y + 140, 36, 15, -0.2, '#3A522B'],
    [tl.x - 30, tl.y + 176, 30, 13, 0.7, '#557540'],
  ] as const) {
    c.fillStyle = col;
    c.beginPath();
    c.ellipse(x, y, rx, ry, rot, 0, Math.PI * 2);
    c.fill();
  }
  c.restore();
  c.restore();

  // The camera's warmth, falloff and grain.
  c.fillStyle = 'rgba(255,170,90,0.06)';
  c.fillRect(0, 0, W, H);
  const vignette = c.createRadialGradient(W / 2, H / 2, 90, W / 2, H / 2, 260);
  vignette.addColorStop(0, 'rgba(0,0,0,0)');
  vignette.addColorStop(1, 'rgba(20,10,0,0.45)');
  c.fillStyle = vignette;
  c.fillRect(0, 0, W, H);
  c.save();
  c.globalAlpha = 0.05;
  c.globalCompositeOperation = 'overlay';
    c.drawImage(noiseFrame(Math.floor(t * 30)), 0, 0, W, H);
  c.restore();

  // Soft focus: the camera is only 0.3 megapixels.
  ctx.save();
  ctx.filter = `blur(${0.45 * scale}px)`;
  ctx.drawImage(camLayer, 0, 0, W, H);
  ctx.restore();
};

export const blankPicture = (ctx: Ctx, r: Rect, color: string) => fillRound(ctx, r, 0, color);

export {drawText, R};
