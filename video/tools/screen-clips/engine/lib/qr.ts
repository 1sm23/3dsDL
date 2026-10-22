import QRCode from 'qrcode';

export type QrMatrix = {size: number; dark: (x: number, y: number) => boolean};

const cache = new Map<string, QrMatrix>();

// Module grid for a URL, without the quiet zone. Medium error correction,
// like the codes the website prints.
export const qrMatrix = (text: string): QrMatrix => {
  const hit = cache.get(text);
  if (hit) return hit;
  const qr = QRCode.create(text, {errorCorrectionLevel: 'M'});
  const size = qr.modules.size;
  const data = qr.modules.data;
  const m = {size, dark: (x: number, y: number) => data[y * size + x] === 1};
  cache.set(text, m);
  return m;
};

// Draws the code with a quiet zone of `quiet` modules into a square.
export const drawQr = (
  ctx: CanvasRenderingContext2D,
  text: string,
  x: number,
  y: number,
  side: number,
  opts: {quiet?: number; dark?: string; light?: string | null} = {},
) => {
  const {quiet = 4, dark = '#141414', light = '#ffffff'} = opts;
  const m = qrMatrix(text);
  const n = m.size + quiet * 2;
  const cell = side / n;
  if (light) {
    ctx.fillStyle = light;
    ctx.fillRect(x, y, side, side);
  }
  ctx.fillStyle = dark;
  for (let r = 0; r < m.size; r++) {
    for (let c = 0; c < m.size; c++) {
      if (!m.dark(c, r)) continue;
      // Overlap by a hair so no seams show between modules.
      ctx.fillRect(x + (c + quiet) * cell, y + (r + quiet) * cell, cell + 0.35, cell + 0.35);
    }
  }
};
