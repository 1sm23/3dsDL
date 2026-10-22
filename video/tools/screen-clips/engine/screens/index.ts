import {loadFonts} from '../lib/fonts';
import {Timeline} from './engine';
import {loadPictures} from './pictures';
import {boot, homeIdle, install, qrScan} from './seqMain';
import {files} from './seqFiles';
import {moreClock, moreHex, moreHistory, moreReport, moreSettings, moreSha, moreUpdates} from './seqMore';
import {receive} from './seqReceive';
import {titles} from './seqTitles';
import {qolExists, qolFolders, qolHttps, qolQueue, qolResume, qolZip} from './seqQol';
import {setDeviceScale, setFontsReady} from './text';

// The 3DS screens of the launch video: the real 3dsDL interface redrawn on
// canvases and animated over time. drawSequence is a pure function of the
// sequence and the time, so frames can render in any order.

export type SeqId =
  | 'boot'
  | 'home'
  | 'qrScan'
  | 'install'
  | 'qolQueue'
  | 'qolResume'
  | 'qolExists'
  | 'qolFolders'
  | 'qolZip'
  | 'qolHttps'
  | 'files'
  | 'titles'
  | 'receive'
  | 'moreHistory'
  | 'moreUpdates'
  | 'moreClock'
  | 'moreHex'
  | 'moreSha'
  | 'moreSettings'
  | 'moreReport';

export type ScreenCue = {
  t: number;
  kind: 'tap' | 'move' | 'select' | 'back' | 'chime' | 'done' | 'installed' | 'notify' | 'error' | 'type' | 'shutter' | 'boot';
};
// Bottom-screen console pixels (0..320, 0..240), when the stylus touches.
export type Tap = {t: number; x: number; y: number; dur?: number};

const TIMELINES: Record<SeqId, Timeline> = {
  boot: boot(),
  home: homeIdle(),
  qrScan: qrScan(),
  install: install(),
  qolQueue: qolQueue(),
  qolResume: qolResume(),
  qolExists: qolExists(),
  qolFolders: qolFolders(),
  qolZip: qolZip(),
  qolHttps: qolHttps(),
  files: files(),
  titles: titles(),
  receive: receive(),
  moreHistory: moreHistory(),
  moreUpdates: moreUpdates(),
  moreClock: moreClock(),
  moreHex: moreHex(),
  moreSha: moreSha(),
  moreSettings: moreSettings(),
  moreReport: moreReport(),
};

export const SEQUENCES: Record<SeqId, {duration: number; cues: ScreenCue[]; taps: Tap[]}> = Object.fromEntries(
  Object.entries(TIMELINES).map(([id, tl]) => [id, tl.info()]),
) as Record<SeqId, {duration: number; cues: ScreenCue[]; taps: Tap[]}>;

export const SEQ_IDS = Object.keys(TIMELINES) as SeqId[];

let assets: Promise<void> | null = null;

// Fonts, the app's pictures and icons. Call once before drawing.
export const loadScreenAssets = (): Promise<void> => {
  if (!assets) assets = Promise.all([loadFonts(), loadPictures()]).then(() => setFontsReady());
  return assets;
};

// Draws both screens of sequence id at t seconds (held on the last frame
// past its duration). scale is canvas pixels per console pixel (5 for the
// console's own screens, so 2000 x 1200 and 1600 x 1200). `origin` puts
// the screen's top left corner at those canvas pixels (default 0, 0) and
// `stretch` widens it, for a canvas that shows one window of a screen.
export const drawSequence = (
  id: SeqId,
  t: number,
  top: CanvasRenderingContext2D,
  bottom: CanvasRenderingContext2D,
  scale: number,
  origin: [number, number] = [0, 0],
  stretch = 1,
) => {
  setDeviceScale(scale);
  for (const ctx of [top, bottom]) {
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    // Nothing from an earlier frame or sequence may show through, even on a
    // canvas bigger than the screen.
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = 'source-over';
    ctx.fillStyle = '#000000';
    ctx.fillRect(0, 0, ctx.canvas.width, ctx.canvas.height);
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = 'source-over';
    ctx.filter = 'none';
    ctx.setTransform(scale * stretch, 0, 0, scale, origin[0], origin[1]);
  }
  try {
    TIMELINES[id].draw(t, top, bottom);
  } finally {
    top.restore();
    bottom.restore();
  }
};
