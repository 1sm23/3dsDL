import {IN_OUT, prog} from './anim';
import {T, UP, UP_PANEL, folderAt} from './script';

// Where things sit on the page, in CSS px from the top-left of the page's
// viewport at scroll 0. Measured from this component's own render of the
// page CSS (system-ui at 16px, as the console's page uses); see
// preview-entry.ts. Horizontal positions are relative to the page's centre
// column (main is max-width 48rem, centred).

export const M = {
  mainMax: 768,
  pad: 16,
  // Links tab.
  tabsMid: 101,
  boxMid: 286,
  sendMid: 389,
  sendDx: 284, // from the page centre
  // Files tab, with no status card and no upload panel.
  listTop: 315.4,
  listHead: 37,
  rowH: 62.36,
  listPadBottom: 6,
  afterList: 14 + 37.7 + 140, // the hint, then main's bottom padding
  cardH: 84.3, // status card plus its margin
  upsH: 130.6, // upload panel plus its margin
  upsClearDx: 189, // Clear Finished, from the content's left edge
  upsClearDy: 91.6, // and from the panel's top
  // The details sheet is centred in the viewport: the top of its acts grid
  // from the viewport's middle, then the grid's pitch.
  sheetW: 480,
  actsMidCia: 85.1,
  actsMidFile: 36.2,
  actsPad: 12,
  actColW: 141.7,
  actGap: 8,
  actRowH: 46.1,
  actBtnH: 38.1,
  actsPadX: 20,
};

export type Layout = {
  tabW: number;
  scroll: number;
  stuck: number;
  contentLeft: number;
};

export const centre = (w: number) => w / 2;
export const contentW = (w: number) => Math.min(M.mainMax, w) - 2 * M.pad;
export const contentLeft = (w: number) => (w - Math.min(M.mainMax, w)) / 2 + M.pad;

const showCard = (t: number) => t >= T.installSent && t < T.installDone;
const showUps = (t: number) => t >= UP_PANEL && t < T.clearClick;

// The top of the list box at t.
export const listTop = (t: number) => M.listTop + (showCard(t) ? M.cardH : 0) + (showUps(t) ? M.upsH : 0);
export const rowTop = (t: number, i: number) => listTop(t) + M.listHead + i * M.rowH;
export const pageHeight = (t: number) =>
  listTop(t) + M.listHead + folderAt(t).entries.length * M.rowH + M.listPadBottom + M.afterList;

// scrollIntoView({block: 'center'}) for row i, clamped as the browser does.
const centred = (t: number, i: number, vh: number) =>
  Math.max(0, Math.min(pageHeight(t) - vh, rowTop(t, i) + M.rowH / 2 - vh / 2));

export const scrollAt = (t: number, vh: number): number => {
  if (t < T.filesShown) return 0;
  const first = UP[0];
  // reveal() of the first uploaded row: smooth, about 0.45 s.
  const index = folderAt(first.shown).entries.findIndex((e) => e.name === first.name);
  const s1 = centred(first.shown, index, vh);
  if (t < first.shown) return 0;
  if (t < T.installSent) return s1 * IN_OUT(prog(t, first.shown + 0.02, first.shown + 0.47));
  // The status card is added above what is on screen; scroll anchoring keeps
  // the view still, then the wheel brings the page back to the top.
  const s2 = s1 + M.cardH;
  return s2 * (1 - IN_OUT(prog(t, T.scrollUp, T.scrollUpEnd)));
};

// How far the sticky upload panel is pushed down to stay at top: 8px.
export const stuckAt = (t: number, vh: number) => {
  const s = scrollAt(t, vh);
  const top = M.listTop + (showCard(t) ? M.cardH : 0);
  return Math.max(0, s + 8 - top);
};

export const layout = (t: number, w: number, vh: number): Layout => ({
  tabW: (contentW(w) - 6) / 2,
  scroll: scrollAt(t, vh),
  stuck: showUps(t) ? stuckAt(t, vh) : 0,
  contentLeft: contentLeft(w),
});
