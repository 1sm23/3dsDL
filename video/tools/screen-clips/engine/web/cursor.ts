import {IN_OUT, clamp, prog, wobble} from './anim';
import {M, contentLeft, contentW, rowTop, scrollAt} from './geometry';
import {sheetActions} from './Page';
import {DOWNLOADED_FILE, REMOTE_INSTALL, T, folderAt} from './script';

// The mouse: where it is, what it presses and what it hovers, as functions of
// t. Coordinates are window px (0,0 is the window's top-left corner).

export const TAB_H = 42;
export const TOOL_H = 44;
export const CHROME_H = TAB_H + TOOL_H + 1;

type Step = {start: number; at: number; x: number; y: number; arc?: number; hover?: string; until?: number};
export type Click = {t: number; key: string};

export const CLICKS: Click[] = [
  {t: T.addrClick, key: 'addr'},
  {t: T.boxClick, key: 'box'},
  {t: T.sendClick, key: 'send'},
  {t: T.filesClick, key: 'tab-files'},
  {t: T.rowClick, key: 'row:/' + REMOTE_INSTALL},
  {t: T.installClick, key: 'act:Install on 3DS'},
  {t: T.clearClick, key: 'clear'},
  {t: T.lumaClick, key: 'row:/luma'},
  {t: T.shotsClick, key: 'row:/luma/screenshots'},
  {t: T.fileClick, key: 'row:/luma/screenshots/' + DOWNLOADED_FILE.name},
  {t: T.downloadClick, key: 'act:Download'},
];

const rowAt = (t: number, w: number, vh: number, name: string) => {
  const i = folderAt(t).entries.findIndex((e) => e.name === name);
  return {
    x: contentLeft(w) + 112 + Math.min(40, name.length * 4),
    y: CHROME_H + rowTop(t, Math.max(0, i)) + M.rowH * 0.42 - scrollAt(t, vh),
  };
};

// The centre of a button in the details sheet.
const sheetButton = (w: number, vh: number, file: string, label: string, cia: boolean) => {
  const acts = CHROME_H + vh / 2 + (cia ? M.actsMidCia : M.actsMidFile) + M.actsPad;
  const left = w / 2 - M.sheetW / 2 + M.actsPadX;
  const n = sheetActions(file).findIndex(([l]) => l === label);
  const col = n % 3;
  const row = Math.floor(n / 3);
  return {
    x: left + col * (M.actColW + M.actGap) + M.actColW * 0.4,
    y: acts + row * M.actRowH + M.actBtnH * 0.55,
  };
};

export const steps = (w: number, h: number): Step[] => {
  const vh = h - CHROME_H;
  const cx = w / 2;
  const cl = contentLeft(w);
  const cw = contentW(w);
  const tabW = (cw - 6) / 2;
  const s: Step[] = [];
  const add = (st: Step) => s.push(st);
  add({start: -1, at: 0, x: cx - 120, y: CHROME_H + vh * 0.5});
  // New tab: click the address field and type.
  add({start: 0.0, at: T.addrClick - 0.05, x: 330, y: TAB_H + TOOL_H / 2 + 3, arc: -0.12});
  add({start: T.enter + 0.15, at: T.pageOpens + 0.1, x: cx + 140, y: CHROME_H + 250, arc: 0.1});
  // Links: click into the box, paste, Send.
  add({start: T.boxClick - 0.55, at: T.boxClick - 0.1, x: cl + cw * 0.58, y: CHROME_H + M.boxMid + 6, arc: 0.12});
  add({start: T.linkPasted + 0.35, at: T.sendClick - 0.25, x: cx + M.sendDx + 6, y: CHROME_H + M.sendMid + 4, arc: -0.1, hover: 'send', until: T.sent + 0.1});
  add({start: T.sent + 0.25, at: T.sent + 0.85, x: cx + cw / 2 + 120, y: CHROME_H + 330, arc: 0.15});
  // Files tab.
  add({start: T.filesClick - 0.42, at: T.filesClick - 0.08, x: cl + 3 + tabW * 1.5 + 10, y: CHROME_H + M.tabsMid + 3, arc: -0.12});
  // Off the right edge to fetch the files...
  add({start: T.filesClick + 0.1, at: T.dragEnter - 0.02, x: w + 40, y: CHROME_H + 240, arc: 0.05});
  add({start: T.dragEnter - 0.02, at: T.dragEnter, x: w + 120, y: CHROME_H + 300});
  // ...and back with them, dropped on the list.
  add({start: T.dragEnter, at: T.dropped - 0.12, x: cx + 70, y: CHROME_H + 470, arc: 0.12});
  add({start: T.dropped + 0.25, at: T.dropped + 1.4, x: cx + 170, y: CHROME_H + 430, arc: 0.1});
  // Install: the row, then the sheet's Install on 3DS.
  const row = rowAt(T.rowClick, w, vh, REMOTE_INSTALL);
  add({start: T.rowClick - 0.5, at: T.rowClick - 0.08, x: row.x, y: row.y, arc: -0.1});
  const inst = sheetButton(w, vh, REMOTE_INSTALL, 'Install on 3DS', true);
  add({start: T.metaLoaded + 0.02, at: T.installClick - 0.08, x: inst.x, y: inst.y, arc: 0.08, hover: 'act:Install on 3DS', until: T.installClick + 0.04});
  // Clear Finished, once the page is back at the top.
  const clear = {x: cl + M.upsClearDx, y: CHROME_H + M.listTop + M.cardH + M.upsClearDy};
  add({start: T.scrollUpEnd + 0.05, at: T.clearClick - 0.1, x: clear.x, y: clear.y, arc: -0.1, hover: 'clear', until: T.clearClick + 0.02});
  add({start: T.clearClick + 0.3, at: T.clearClick + 0.9, x: cx + 230, y: CHROME_H + 300, arc: 0.1});
  // Into /luma/screenshots and the download.
  const luma = rowAt(T.lumaClick - 0.01, w, vh, 'luma');
  add({start: T.lumaClick - 0.5, at: T.lumaClick - 0.08, x: luma.x, y: luma.y, arc: 0.1});
  const shots = rowAt(T.shotsClick - 0.01, w, vh, 'screenshots');
  add({start: T.lumaShown + 0.02, at: T.shotsClick - 0.06, x: shots.x, y: shots.y, arc: -0.08});
  const file = rowAt(T.fileClick - 0.01, w, vh, DOWNLOADED_FILE.name);
  add({start: T.shotsShown + 0.02, at: T.fileClick - 0.06, x: file.x, y: file.y, arc: 0.08});
  const dl = sheetButton(w, vh, DOWNLOADED_FILE.name, 'Download', false);
  add({start: T.fileClick + 0.12, at: T.downloadClick - 0.07, x: dl.x, y: dl.y, arc: -0.08, hover: 'act:Download', until: T.downloadClick + 0.04});
  add({start: T.downloadClick + 0.25, at: T.downloadClick + 0.95, x: cx + 140, y: CHROME_H + 360, arc: 0.12});
  return s;
};

// Where the cursor is at t.
export const cursorAt = (t: number, w: number, h: number) => {
  const s = steps(w, h);
  let x = s[0].x;
  let y = s[0].y;
  let restSince = 0;
  for (let i = 1; i < s.length; i++) {
    const st = s[i];
    if (t < st.start) break;
    const p = IN_OUT(prog(t, st.start, st.at));
    const dx = st.x - x;
    const dy = st.y - y;
    const bend = (st.arc || 0) * Math.sin(Math.PI * p);
    const nx = x + dx * p - dy * bend;
    const ny = y + dy * p + dx * bend;
    restSince = st.at;
    x = nx;
    y = ny;
    if (t < st.at) {
      restSince = Infinity;
      break;
    }
  }
  // A hand at rest is never quite still.
  if (restSince !== Infinity) {
    const k = clamp((t - restSince) / 0.6) * 1.6;
    x += wobble(t, 1) * k;
    y += wobble(t, 2) * k;
  }
  return {x, y};
};

export const hoverKey = (t: number, w: number, h: number): string | null => {
  for (const st of steps(w, h)) if (st.hover && t >= st.at - 0.05 && t < (st.until ?? st.at + 0.3)) return st.hover;
  return null;
};

export const pressKey = (t: number): string | null => {
  for (const c of CLICKS) if (t >= c.t && t < c.t + 0.12) return c.key;
  return null;
};

export const pressAmount = (t: number) => {
  for (const c of CLICKS.concat([{t: T.dropped, key: 'drop'}])) {
    if (t >= c.t - 0.06 && t < c.t + 0.16) {
      const d = t - c.t;
      return d < 0 ? IN_OUT((d + 0.06) / 0.06) : 1 - IN_OUT(d / 0.16);
    }
  }
  return 0;
};
