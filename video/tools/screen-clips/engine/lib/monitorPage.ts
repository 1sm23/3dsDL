import {drawQr} from './qr';
import {DEMO_CIA_BYTES, DEMO_CIA_NAME, DEMO_CIA_URL, DEMO_VERSION} from './links';
import {ROUNDED} from './fonts';

// The page drawn on the desk monitor in the Scan scene, and again in the
// 3DS camera's view of that monitor, so both show the same thing.
// Page geometry, in page pixels (1920 x 1080), for anyone framing the code:
export const MONITOR_PAGE = {w: 1920, h: 1080, qr: {x: 1110, y: 245, side: 660}};

// What the monitor shows: a simple release page with the real QR code for
// sh23ds.cia, big enough to scan from across a desk.
export const drawMonitorPage = (g: CanvasRenderingContext2D, w: number, h: number, icon: CanvasImageSource | null) => {
  const bg = g.createLinearGradient(0, 0, 0, h);
  bg.addColorStop(0, '#fbfaf6');
  bg.addColorStop(1, '#efece3');
  g.fillStyle = bg;
  g.fillRect(0, 0, w, h);
  // browser strip
  g.fillStyle = '#e4e0d6';
  g.fillRect(0, 0, w, 70);
  g.fillStyle = '#ffffff';
  g.beginPath();
  g.roundRect(220, 16, w - 440, 38, 19);
  g.fill();
  g.fillStyle = '#8c8466';
  g.font = `500 22px ${ROUNDED}`;
  g.textBaseline = 'middle';
  g.fillText('github.com/ankushKun/sh23ds/releases/latest', 250, 36);
  ['#ec6a5e', '#f4bf4f', '#61c554'].forEach((c, i) => {
    g.fillStyle = c;
    g.beginPath();
    g.arc(40 + i * 30, 35, 9, 0, Math.PI * 2);
    g.fill();
  });
  // left column: app and words
  if (icon) {
    g.save();
    g.beginPath();
    g.roundRect(150, 200, 170, 170, 38);
    g.clip();
    g.drawImage(icon, 150, 200, 170, 170);
    g.restore();
  }
  g.fillStyle = '#2b2620';
  g.font = `800 92px ${ROUNDED}`;
  g.textBaseline = 'alphabetic';
  g.fillText('sh23ds', 150, 480);
  g.fillStyle = '#7a725f';
  g.font = `500 38px ${ROUNDED}`;
  g.fillText('Silent Hill 2 for Nintendo 3DS.', 150, 540);
  g.fillStyle = '#1c93c2';
  g.font = `700 36px ${ROUNDED}`;
  g.fillText(`Scan to download ${DEMO_CIA_NAME}`, 150, 640);
  g.fillStyle = '#a39b82';
  g.font = `500 28px ${ROUNDED}`;
  g.fillText(`${(DEMO_CIA_BYTES / 1048576).toFixed(1)} MiB  ·  v${DEMO_VERSION}`, 150, 690);
  // the code
  const side = Math.min(h - 200, 660);
  const qx = w - side - 150;
  const qy = (h + 70 - side) / 2;
  g.fillStyle = 'rgba(0,0,0,0.08)';
  g.beginPath();
  g.roundRect(qx - 6, qy + 8, side + 12, side + 12, 28);
  g.fill();
  g.fillStyle = '#fff';
  g.beginPath();
  g.roundRect(qx - 10, qy - 10, side + 20, side + 20, 28);
  g.fill();
  drawQr(g, DEMO_CIA_URL, qx, qy, side, {quiet: 3});
};

