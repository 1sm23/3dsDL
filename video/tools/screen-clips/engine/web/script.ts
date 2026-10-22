import {RECEIVE, SENT_LINK, SENT_NAME, UPLOADS, REMOTE_INSTALL, DOWNLOADED_FILE} from '../scripts/receive';
import {size} from './anim';

// The browser's half of the Receive scene. The fixed moments come from
// src/scripts/receive.ts; the in-between ones (polls, list loads, the 3DS
// answering its dialogs) are placed here so the page reacts the way the
// real one does.

const R = RECEIVE;

export const T = {
  // The address is typed into a new tab, then the page loads.
  addrClick: 0.3,
  typeStart: 0.5,
  typeEnd: 1.1,
  enter: 1.3,
  pageOpens: R.pageOpens,
  ready: R.pageOpens + 0.35, // first /api/status answer
  // Links tab.
  boxClick: 3.7,
  linkPasted: R.linkPasted,
  sendClick: R.linkSent,
  sent: R.linkSent + 0.22, // POST /send answered
  // The 3DS shows its "Received 1 link" dialog at linkReceived and Download
  // is chosen a moment later.
  linkDownloading: R.linkReceived + 0.8,
  linkDone: R.linkDownloaded + 0.05,
  // Files tab.
  filesClick: 9.72,
  filesShown: 9.9,
  // The drag from outside the window.
  dragEnter: R.dropStart,
  overWindow: R.dropStart + 0.16, // dragenter: the drop overlay shows
  dropped: R.dropped,
  // Uploads, one at a time through the page's single lane.
  uploadsDone: R.uploadsDone,
  // Install from the details sheet.
  rowClick: R.installClicked - 0.42,
  metaLoaded: R.installClicked - 0.22,
  installClick: R.installClicked,
  installSent: R.installClicked + 0.12,
  installing: R.installClicked + 0.75, // Install chosen on the 3DS
  installDone: R.installDone + 0.02,
  scrollUp: R.installClicked + 0.22,
  scrollUpEnd: R.installClicked + 0.72,
  clearClick: 15.35,
  // Download from /luma/screenshots.
  lumaClick: R.browseClicked,
  lumaShown: R.browseClicked + 0.12,
  shotsClick: R.browseClicked + 0.45,
  shotsShown: R.browseClicked + 0.57,
  fileClick: R.downloadClicked - 0.36,
  downloadClick: R.downloadClicked,
  downloadStart: R.downloadClicked + 0.1,
  downloadDone: R.downloadDone,
};

export const ADDRESS_TYPED = '3ds.local';
export {SENT_LINK, SENT_NAME, REMOTE_INSTALL, DOWNLOADED_FILE};

// --- Uploads -------------------------------------------------------------------
// Each file: its PUT runs [start, end]; the list reload that shows its row
// lands at shown. The page sends them in drop order.
export type Upload = {name: string; bytes: number; start: number; end: number; shown: number};

const upPlan = (): Upload[] => {
  const first = T.dropped + 0.15; // the folder check before uploading
  const listMs = 0.1; // each reload of the list between uploads
  const totalBytes = UPLOADS.reduce((n, u) => n + u.bytes, 0);
  const last = T.uploadsDone - 0.15; // the last upload finishes
  const gaps = UPLOADS.length * listMs;
  const rate = totalBytes / (last - first - gaps + listMs);
  const out: Upload[] = [];
  let at = first;
  for (const u of UPLOADS) {
    const d = Math.max(0.1, u.bytes / rate);
    out.push({name: u.name, bytes: u.bytes, start: at, end: at + d, shown: at + d + listMs * 0.9});
    at += d + listMs;
  }
  return out;
};
export const UP = upPlan();
export const UP_TOTAL = UPLOADS.reduce((n, u) => n + u.bytes, 0);
export const UP_PANEL = T.dropped + 0.12;

// How far a file has gone, with a little unevenness so it reads as a real
// transfer (still monotonic).
export const upLoaded = (u: Upload, t: number) => {
  if (t <= u.start) return 0;
  if (t >= u.end) return u.bytes;
  const p = (t - u.start) / (u.end - u.start);
  const q = p + 0.025 * Math.sin(p * Math.PI * 3) * (1 - p);
  return Math.floor(u.bytes * Math.min(1, Math.max(0, q)));
};

export const upPanel = (t: number) => {
  const done = UP.reduce((n, u) => n + upLoaded(u, t), 0);
  const active = UP.find((u) => t >= u.start && t < u.end) || UP.find((u) => t < u.start) || null;
  const finished = UP.filter((u) => t >= u.end).length;
  const ms = UP.reduce((n, u) => n + Math.max(0, Math.min(t, u.end) - u.start), 0);
  const all = t >= UP[UP.length - 1].end;
  if (all) {
    return {all, title: 'All ' + UP.length + ' uploads finished', sub: size(UP_TOTAL) + ' sent', pct: 100, finished};
  }
  const pct = Math.floor((100 * done) / UP_TOTAL);
  const rate = ms > 1 ? done / ms : 0;
  const now = active || UP[0];
  const at = UP.indexOf(now) + 1;
  const sub = [pct + '%', rate ? size(rate) + '/s' : '', rate ? durationLeft((UP_TOTAL - done) / rate) : ''].filter(Boolean).join(' · ');
  return {all, title: 'Uploading ' + now.name + ' (' + at + ' of ' + UP.length + ')', sub, pct, finished};
};

const durationLeft = (s: number) => 'about ' + Math.max(1, Math.round(s)) + ' s left';

// --- The SD card -----------------------------------------------------------------
export type Entry = {name: string; dir?: boolean; bytes?: number; time: string; shown?: number};

const NOW = '2026-10-24 16:21';

export const ROOT: Entry[] = [
  {name: '3ds', dir: true, time: '2026-10-02 18:11'},
  {name: 'cias', dir: true, time: '2026-10-19 21:47'},
  {name: 'gm9', dir: true, time: '2026-03-08 12:05'},
  {name: 'luma', dir: true, time: '2026-10-24 16:02'},
  {name: 'Nintendo 3DS', dir: true, time: '2025-12-29 10:14'},
  {name: 'boot.3dsx', bytes: 185_544, time: '2026-06-11 09:30'},
  {name: 'boot.firm', bytes: 255_488, time: '2026-08-02 20:16'},
  ...UP.map((u) => ({name: u.name, bytes: u.bytes, time: NOW, shown: u.shown})),
];

export const LUMA: Entry[] = [
  {name: 'payloads', dir: true, time: '2025-12-29 10:20'},
  {name: 'screenshots', dir: true, time: '2026-10-24 16:02'},
  {name: 'config.ini', bytes: 1_101, time: '2026-09-30 22:41'},
];

export const SHOTS: Entry[] = [
  {name: 'bot_0000.bmp', bytes: 230_454, time: '2026-10-24 15:58'},
  {name: 'bot_0001.bmp', bytes: 230_454, time: '2026-10-24 16:02'},
  {name: 'top_0000.bmp', bytes: 288_054, time: '2026-10-24 15:58'},
  {name: DOWNLOADED_FILE.name, bytes: DOWNLOADED_FILE.bytes, time: '2026-10-24 16:02'},
];

// sorted() in remote_page.cpp, "By name": folders first, then by name.
export const byName = (list: Entry[]) =>
  list
    .slice()
    .sort((a, b) =>
      !!a.dir !== !!b.dir ? (a.dir ? -1 : 1) : a.name.localeCompare(b.name, undefined, {numeric: true, sensitivity: 'base'}),
    );

export const SD_FREE = 21_474_836_480;
export const SD_TOTAL = 31_914_983_424;

// Which folder the Files tab shows at t, and what is in it.
export const folderAt = (t: number) => {
  if (t >= T.shotsShown) return {path: '/luma/screenshots', parts: ['luma', 'screenshots'], entries: byName(SHOTS), since: T.shotsShown};
  if (t >= T.lumaShown) return {path: '/luma', parts: ['luma'], entries: byName(LUMA), since: T.lumaShown};
  return {
    path: '/',
    parts: [] as string[],
    entries: byName(ROOT.filter((e) => e.shown === undefined || t >= e.shown)),
    since: T.filesShown,
  };
};

// The Anemone3DS details, as /api/meta reads them from the CIA's SMDH.
export const CIA_META = {
  name: 'Anemone3DS',
  publisher: 'Anemone3DS Team',
  titleId: '000400000BAFE000',
  version: '3.0.1 (v3073)',
};
