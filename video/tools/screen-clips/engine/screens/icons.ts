import {ATLAS_SIZES, ATLAS_STROKE, ICON_PARTS, IconName, IconPart} from './iconData';
import {getDeviceScale} from './text';
import {Ctx} from './theme';

export type {IconName} from './iconData';

const paths = new Map<IconName, Path2D>();

const pathFor = (name: IconName) => {
  const hit = paths.get(name);
  if (hit) return hit;
  const p = new Path2D();
  for (const part of ICON_PARTS[name] as IconPart[]) {
    if ('d' in part) {
      p.addPath(new Path2D(part.d));
    } else if ('rect' in part) {
      const [x, y, w, h, rx] = part.rect;
      const sub = new Path2D();
      if (rx > 0) sub.roundRect(x, y, w, h, rx);
      else sub.rect(x, y, w, h);
      p.addPath(sub);
    } else {
      const [cx, cy, r] = part.circle;
      const sub = new Path2D();
      sub.arc(cx, cy, r, 0, Math.PI * 2);
      p.addPath(sub);
    }
  }
  paths.set(name, p);
  return p;
};

// The atlas cell the console would pick for a size: the smallest at least as
// big, whose stroke the icon is drawn with.
const strokeFor = (size: number) => {
  let pick = ATLAS_SIZES[0];
  for (const cell of ATLAS_SIZES) {
    if (cell >= size - 0.5) pick = cell;
  }
  return ATLAS_STROKE[pick];
};

const sprites = new Map<string, OffscreenCanvas>();

const sprite = (name: IconName, size: number, color: string) => {
  const scale = getDeviceScale();
  const key = `${scale}|${name}|${size}|${color}`;
  const hit = sprites.get(key);
  if (hit) return hit;
  const px = Math.ceil(size * scale) + 2;
  const canvas = new OffscreenCanvas(px, px);
  const ctx = canvas.getContext('2d')!;
  const k = (size * scale) / 24;
  ctx.setTransform(k, 0, 0, k, 1, 1);
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  ctx.lineWidth = strokeFor(size);
  ctx.strokeStyle = color;
  ctx.stroke(pathFor(name));
  if (sprites.size > 600) {
    const first = sprites.keys().next().value;
    if (first !== undefined) sprites.delete(first);
  }
  sprites.set(key, canvas);
  return canvas;
};

// ui::drawIcon: the icon size px square at (x, y), tinted.
export const drawIcon = (ctx: Ctx, name: IconName | null | undefined, x: number, y: number, size: number, color: string) => {
  if (!name) return;
  const canvas = sprite(name, size, color);
  const scale = getDeviceScale();
  const dx = Math.round(x * scale) / scale - 1 / scale;
  const dy = Math.round(y * scale) / scale - 1 / scale;
  ctx.drawImage(canvas, dx, dy, canvas.width / scale, canvas.height / scale);
};
