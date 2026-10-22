import {bytes, DEMO, DESTINATION, HISTORY, rate, duration as fmtDuration} from './data';
import {
  appQr,
  badge,
  bottomBackground,
  button,
  drawAppQr,
  drawText,
  fillRect,
  fillRound,
  panel,
  statusBar,
  tab,
  tagRow,
  topBackground,
  TOP_PANEL,
} from './draw';
import {Timeline, TAP} from './engine';
import {drawIcon} from './icons';
import {
  busyBottom,
  busyTop,
  controlsBottom,
  dialogBottom,
  DialogSpec,
  dialogTop,
  focusAt,
  FocusId,
  listBottom,
  listMotion,
  ListSpec,
  Painter,
  ProgressView,
  ScreenSpec,
} from './kit';
import {getClock} from './clock';
import {chrome, filesSpec, filesTop, footer, settingsSpec, settingsTop, Shot} from './screens';
import {progressShot} from './seqMain';
import {drawMarquee, drawWrapped, lineHeight, textWidth, wrap} from './text';
import {APP_VERSION, C, easeInOut, prng, R, span, T} from './theme';

const at = (h: number, m: number, s = 0) => h * 3600 + m * 60 + s;

// --- History ---------------------------------------------------------------------------------

const badgeColor = (c: string) => (c === 'good' ? C.Good : c === 'bad' ? C.Bad : c === 'tag' ? C.Tag : C.Neutral);

export const moreHistory = () => {
  const tl = new Timeline(2.5, at(20, 9, 0));
  const spec: ListSpec = {
    title: 'Recent Downloads',
    items: HISTORY.map((h) => ({label: h.name, badge: h.badge, badgeColor: badgeColor(h.color)})),
    action: 'Clear',
    actionKey: 'X',
    hint: `${HISTORY.length} saved`,
  };
  const keys: [number, number][] = [
    [0, 0],
    [0.8, 1],
    [1.5, 2],
    [2.1, 3],
  ];
  for (const [t] of keys.slice(1)) tl.cue(t, 'move');
  tl.at(0, (t) => {
    const m = listMotion(spec, keys, t);
    const h = HISTORY[m.cursor];
    return {
      top: (ctx) => {
        chrome(ctx, 'History', 'History', C.IconRed);
        panel(ctx);
        const left = TOP_PANEL.x + 18;
        const width = Math.max(34, textWidth(h.badge, 0.34) + 10);
        badge(ctx, left + width, TOP_PANEL.y + 14, h.badge, badgeColor(h.color));
        drawText(ctx, h.time, left + width + 10, TOP_PANEL.y + 13, T.Small, C.InkSoft);
        drawMarquee(ctx, h.name, left, TOP_PANEL.y + 36, 0.62, C.InkStrong, 330);
        let y = drawWrapped(ctx, h.url, left, TOP_PANEL.y + 62, T.Tiny, C.InkSoft, 330, 2) + 4;
        y = drawWrapped(ctx, h.where, left, y, T.Small, C.Ink, 330, 2) + 4;
        if (h.badge === 'Resume' && y < TOP_PANEL.y + TOP_PANEL.h - 24) {
          tagRow(ctx, left, y, 86, 'Can resume', `1.46 MiB is kept in ${DESTINATION}`, C.Good);
        }
      },
      bottom: (ctx) => listBottom(ctx, spec, {...m, t}),
    };
  });
  return tl;
};

// --- Settings > Updates --------------------------------------------------------------------------

const OLD_VERSION = '0.8.1';

const updatesSpec = (): ScreenSpec => ({
  back: 'Back',
  controls: [
    {rect: R(30, 16, 260, 44), label: `Update to ${APP_VERSION}`, icon: 'Download', iconColor: C.IconGreen, scale: T.Button},
    {rect: R(30, 70, 260, 32), label: `What's New in ${APP_VERSION}`, icon: 'Text', iconColor: C.IconGrey, scale: T.Small + 0.06},
    {rect: R(30, 112, 260, 32), label: 'Check at Startup: On', icon: 'Clock', iconColor: C.IconBlue, scale: T.Small + 0.06},
  ],
  drawBottom: (ctx) =>
    drawWrapped(ctx, "Downloads 3dsDL.cia from GitHub and checks it against the release's checksum before installing it.", 160, 154, T.Tiny, C.InkSoft, 270, 3, 'center'),
});

const updatesTop: Painter = (ctx) => {
  chrome(ctx, 'Software Update', 'Download', C.IconGreen);
  panel(ctx);
  const left = TOP_PANEL.x + 18;
  const top = TOP_PANEL.y + 12;
  fillRound(ctx, R(left, top, 48, 48), 11, C.Accent);
  fillRound(ctx, R(left, top, 48, 24), 11, 'rgba(255,255,255,0.22)', 3);
  drawIcon(ctx, 'AppMark', left + 8, top + 8, 32, '#FFFFFF');
  drawText(ctx, `3dsDL ${APP_VERSION}`, left + 60, top + 2, 0.62, C.InkStrong);
  drawText(ctx, 'Released 2026-10-20', left + 60, top + 30, T.Small, C.InkSoft);
  let y = top + 60;
  y = tagRow(ctx, left, y, 96, 'Installed', OLD_VERSION);
  y = tagRow(ctx, left, y, 96, 'New', `${APP_VERSION}, ${bytes(2_142_208)}`, C.Good);
  drawMarquee(ctx, 'Receive closes at once, and big folders delete fast.', left, y + 2, T.Small, C.Ink, TOP_PANEL.w - 36);
};

export const moreUpdates = () => {
  const tl = new Timeline(2.5, at(20, 10, 0));
  const spec = updatesSpec();
  const keys: [number, FocusId][] = [
    [0, 2],
    [0.5, 1],
    [0.95, 0],
  ];
  tl.cue(0.5, 'move').cue(0.95, 'move');
  const tapAt = 1.45;
  tl.tapRect(tapAt, spec.controls[0].rect);
  tl.at(0, (t) => ({top: updatesTop, bottom: (ctx) => controlsBottom(ctx, spec, {focus: focusAt(keys, t), pressed: Timeline.pressed(t, tapAt) ? 0 : null, t})}));
  tl.cue(tapAt + TAP, 'select');
  // Getting the release's checksums first, then the download.
  const under = (ctx: CanvasRenderingContext2D) => controlsBottom(ctx, spec, {focus: focusAt(keys, 2), t: 1.6});
  tl.at(
    tapAt + TAP,
    (t, local) => ({
      top: (ctx) => busyTop(ctx, 'Software Update', "Getting the release's checksums...", updatesTop),
      bottom: (ctx) => busyBottom(ctx, under, local, t),
    }),
    0,
  );
  const dl = 1.95;
  const total = 2_142_208;
  tl.at(dl, (t) => {
    const f = 0.42 * easeInOut(span(t, dl + 0.05, 2.5));
    const done = Math.round(total * f);
    const speed = 1024 * (575 + 10 * Math.sin(Math.floor(t * 4)));
    const v: ProgressView = {
      title: 'Downloading',
      icon: 'Download',
      iconColor: C.IconGreen,
      name: '3dsDL.cia',
      source: 'from github.com',
      fraction: f,
      amounts: `${bytes(done)} of ${bytes(total)}`,
      speed: t < dl + 0.3 ? 'Measuring speed...' : `${rate(speed)}, ${fmtDuration((total - done) / speed)} left`,
      tag: '4 connections',
      message: 'Saving to sdmc:/3ds/3dsDL/',
      detail: footer,
    };
    return progressShot(v, t);
  });
  return tl;
};

// --- Console Clock ----------------------------------------------------------------------------------

export const moreClock = () => {
  const tl = new Timeline(2.5, at(20, 16, 57));
  const spec: ScreenSpec = {
    back: 'Back',
    controls: [
      {rect: R(26, 26, 268, 40), label: 'Set the Clock'},
      {rect: R(26, 80, 268, 40), label: 'Change Time Zone'},
    ],
    drawBottom: (ctx) =>
      drawWrapped(ctx, 'Setting the clock needs the CIA version of 3dsDL. Rosalina can also set the time.', 160, 136, T.Tiny, C.InkSoft, 260, 3, 'center'),
  };
  const keys: [number, FocusId][] = [
    [0, 1],
    [1.1, 0],
    [1.8, 1],
  ];
  tl.cue(1.1, 'move').cue(1.8, 'move');
  tl.at(0, (t) => ({
    top: (ctx) => {
      chrome(ctx, 'Console Clock', 'Clock', C.IconBlue);
      panel(ctx);
      const left = TOP_PANEL.x + 18;
      const time = getClock().split('  ')[1];
      let y = TOP_PANEL.y + 14;
      y = tagRow(ctx, left, y, 108, 'This console', `2026-10-20 ${time}`);
      y = tagRow(ctx, left, y, 108, 'Internet time', `2026-10-20 ${time}`, C.Good);
      y = tagRow(ctx, left, y, 108, 'Time zone', 'UTC+05:30, India, Sri Lanka', C.Neutral, t);
      drawWrapped(ctx, 'The clock is right.', 200, y + 6, T.Small, C.Good, 330, 2, 'center');
    },
    bottom: (ctx) => controlsBottom(ctx, spec, {focus: focusAt(keys, t), t}),
  }));
  return tl;
};

// --- Hex view -----------------------------------------------------------------------------------

// The start of 3dsFM.3dsx: its header, then ARM code.
const HEX_BYTES = (() => {
  const out: number[] = [];
  const header = [0x33, 0x44, 0x53, 0x58, 0x2c, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00];
  out.push(...header);
  const words = [0x0013a1c4, 0x00004e3c, 0x0002b6a0, 0x00098d10, 0x0000002c, 0x00000000];
  for (const w of words) out.push(w & 255, (w >> 8) & 255, (w >> 16) & 255, (w >>> 24) & 255);
  out.push(0x00, 0x00, 0x00, 0x00, 0x14, 0x05, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
  const r = prng(31);
  const ops = [0xe5, 0xe1, 0xe3, 0xeb, 0xe2, 0xe9, 0xe8, 0xea, 0x1a, 0x0a];
  while (out.length < 512) {
    out.push(Math.floor(r() * 256), Math.floor(r() * 256) & 0x9f, Math.floor(r() * 256) & 0x3f, ops[Math.floor(r() * ops.length)]);
  }
  // A string in the code: the app's name, for the characters column.
  const name = Array.from('3dsFM by ankushKun').map((ch) => ch.charCodeAt(0));
  out.splice(0x98, name.length, ...name);
  return out;
})();

const hexTop =
  (top: number): Painter =>
  (ctx) => {
    topBackground(ctx);
    statusBar(ctx, getClock());
    const name = '3dsFM.3dsx';
    const width = 28 + Math.min(300, textWidth(name, T.Title));
    drawIcon(ctx, 'Hex', 200 - width / 2, 22, 22, C.IconGrey);
    drawMarquee(ctx, name, 200 - width / 2 + 28, 21, T.Title, C.Accent, 300);
    const paper = R(10, 50, 380, 184);
    fillRound(ctx, R(paper.x - 1, paper.y - 1, paper.w + 2, paper.h + 2), 7, '#CFC48A');
    fillRound(ctx, paper, 6, '#FFFDF4');
    for (let row = 0; row < 12; row++) {
      const first = top + row * 8;
      const y = paper.y + 5 + row * 14.5;
      drawText(ctx, first.toString(16).toUpperCase().padStart(8, '0'), 20, y, 0.34, '#A79D70');
      for (let col = 0; col < 8; col++) {
        const v = HEX_BYTES[first + col] ?? 0;
        drawText(ctx, v.toString(16).toUpperCase().padStart(2, '0'), 100 + col * 25 + (col >= 4 ? 8 : 0), y, 0.34, C.InkStrong);
        drawText(ctx, v >= 0x20 && v < 0x7f ? String.fromCharCode(v) : '.', 318 + col * 8.5, y, 0.34, C.IconGreen);
      }
    }
  };

export const moreHex = () => {
  const tl = new Timeline(2.5, at(20, 12, 0));
  const pageDown = R(164, 72, 140, 32);
  const tapAt = 1.2;
  tl.tapRect(tapAt, pageDown);
  tl.at(0, (t) => {
    const top = t >= tapAt + TAP ? 0x60 : 0;
    return {
      top: hexTop(top),
      bottom: (ctx) => {
        bottomBackground(ctx);
        const p = R(13, 10, 294, 50);
        panel(ctx, p);
        drawText(ctx, `Offset 0x${top.toString(16).toUpperCase()} of ${bytes(1_701_888)}`, 160, p.y + 6, T.Small, C.Ink, 'center');
        drawText(ctx, 'Drag or flick to scroll, L and R for a page.', 160, p.y + 27, T.Tiny, C.InkSoft, 'center');
        const s = T.Small + 0.02;
        button(ctx, R(16, 72, 140, 32), 'Page Up', {scale: s});
        button(ctx, pageDown, 'Page Down', {scale: s, pressed: Timeline.pressed(t, tapAt)});
        button(ctx, R(16, 114, 140, 32), 'Go to Start', {scale: s});
        button(ctx, R(164, 114, 140, 32), 'Go to End', {scale: s});
        tab(ctx, 'left', 'Back');
      },
    };
  });
  return tl;
};

// --- SHA-256 ------------------------------------------------------------------------------------------

const HASH = '4f2a9c7e1b05d3e8a6c41f9b2d7e05c3a8f16b4e92d07c5a3e18b6f40c9d2a71';

export const SHA_DIALOG: DialogSpec = {
  header: 'SHA-256',
  title: 'sh23ds.cia is intact.',
  tone: 'success',
  body: [
    {text: 'sh23ds.cia', maxLines: 2},
    {text: `${HASH.slice(0, 32)}\n${HASH.slice(32)}`, maxLines: 2},
    {text: 'It matches sh23ds.sha256.', color: C.Good, maxLines: 3},
  ],
  buttons: ['Close'],
};

export const moreSha = () => {
  const tl = new Timeline(2.5, at(20, 12, 30));
  const entries = [
    {name: 'old', folder: true, items: 4, changed: '2026-10-02 18:40'},
    {name: '3dsDL.cia', bytes: 1_950_720, changed: '2026-10-19 20:02'},
    {name: 'sh23ds.cia', bytes: DEMO.bytes, changed: '2026-10-20 20:08'},
    {name: 'sh23ds.sha256', bytes: 155, changed: '2026-10-20 20:08'},
    {name: 'banner.jpg', bytes: 92_467, changed: '2026-10-11 09:15'},
  ];
  const spec = filesSpec(DESTINATION, entries, false);
  const under: Shot = {
    top: filesTop('cias', DESTINATION, entries[2], {}),
    bottom: (ctx) => listBottom(ctx, spec, {cursor: 2, t: 0}),
  };
  tl.at(0, (t, local) => ({
    top: (ctx) => busyTop(ctx, 'SHA-256', 'Reading sh23ds.cia...', under.top),
    bottom: (ctx) => busyBottom(ctx, under.bottom, local + 0.3, t),
  }));
  tl.at(
    1.0,
    (t, local) => ({
      top: (ctx) => dialogTop(ctx, SHA_DIALOG),
      bottom: (ctx) => dialogBottom(ctx, SHA_DIALOG, {selected: 0, age: local + 0.2, t}, under.bottom),
    }),
    0.08,
  );
  tl.cue(1.0, 'done');
  return tl;
};

// --- Settings -------------------------------------------------------------------------------------------

export const moreSettings = () => {
  const tl = new Timeline(2.5, at(20, 11, 0));
  const spec = settingsSpec();
  const keys: [number, FocusId][] = [
    [0, 2],
    [0.6, 3],
    [1.2, 5],
    [1.8, 4],
  ];
  for (const [t] of keys.slice(1)) tl.cue(t, 'move');
  tl.at(0, (t) => {
    const f = focusAt(keys, t);
    return {top: settingsTop(f.id as number), bottom: (ctx) => controlsBottom(ctx, spec, {focus: f, t})};
  });
  return tl;
};

// --- Report an Issue -------------------------------------------------------------------------------------

const DETAILS = `3dsDL ${APP_VERSION} (CIA) on New Nintendo 3DS XL, 11.17.0-50E`;
const encode = (v: string) => Array.from(new TextEncoder().encode(v)).map((b) => (/[A-Za-z0-9\-._~]/.test(String.fromCharCode(b)) ? String.fromCharCode(b) : '%' + b.toString(16).toUpperCase().padStart(2, '0'))).join('');

const PLACES = [
  {
    label: 'Report a Bug',
    icon: 'Warning' as const,
    color: C.IconOrange,
    text: "Opens a new issue with your console's details filled in.",
    shown: 'github.com/ankushKun/3dsDL/issues',
    link: `https://github.com/ankushKun/3dsDL/issues/new?template=bug.yml&details=${encode(DETAILS)}`,
    compact: true,
  },
  {label: 'Discord Server', icon: 'Chip' as const, color: C.IconPink, text: 'Ask questions and share ideas with other users.', shown: 'discord.gg/axrWTjyAWG', link: 'https://discord.gg/axrWTjyAWG', compact: false},
  {label: 'Developer on X', icon: 'Globe' as const, color: C.IconTeal, text: "For quick questions, or if you don't use GitHub.", shown: 'x.com/ankushKun_', link: 'https://x.com/ankushKun_', compact: false},
];

const reportTop =
  (index: number): Painter =>
  (ctx) => {
    const place = PLACES[index];
    chrome(ctx, 'Report an Issue', 'Warning', C.IconOrange);
    panel(ctx);
    const code = appQr(place.link, place.compact);
    const column = 45 * 3 + 12;
    const px = code.size <= 25 ? 4 : 3;
    const side = code.size * px;
    drawAppQr(ctx, code, TOP_PANEL.x + 12 + (column - side) / 2, TOP_PANEL.y + (TOP_PANEL.h - side) / 2, px);
    const left = TOP_PANEL.x + 12 + column + 8;
    const width = TOP_PANEL.x + TOP_PANEL.w - 10 - left;
    const lines = wrap(place.text, T.Small, width, 4).length;
    const block = lineHeight(T.Small) * (1 + lines) + 4 + 10 + lineHeight(T.Tiny);
    let y = TOP_PANEL.y + (TOP_PANEL.h - block) / 2;
    drawMarquee(ctx, place.label, left, y, T.Small, C.InkStrong, width);
    y += lineHeight(T.Small) + 4;
    y = drawWrapped(ctx, place.text, left, y, T.Small, C.Ink, width, 4) + 10;
    drawMarquee(ctx, place.shown, left, y, T.Tiny, C.Accent, width);
  };

export const moreReport = () => {
  const tl = new Timeline(2.5, at(20, 11, 30));
  const spec: ScreenSpec = {
    back: 'Back',
    controls: PLACES.map((p, i) => ({rect: R(30, 12 + i * 44, 260, 36), label: p.label, icon: p.icon, iconColor: p.color, scale: T.Small + 0.06})),
    drawBottom: (ctx) => {
      drawText(ctx, 'Scan the code above with a phone.', 160, 142, T.Tiny, C.InkSoft, 'center');
      const p = R(13, 160, 294, 42);
      panel(ctx, p);
      drawText(ctx, 'Mention this in your report:', 160, p.y + 5, T.Tiny, C.InkSoft, 'center');
      drawMarquee(ctx, DETAILS, 160, p.y + 22, T.Tiny, C.Ink, p.w - 16, 'center');
    },
  };
  const keys: [number, FocusId][] = [
    [0, 0],
    [0.9, 1],
    [1.7, 2],
  ];
  tl.cue(0.9, 'move').cue(1.7, 'move');
  tl.at(0, (t) => {
    const f = focusAt(keys, t);
    return {top: reportTop(f.id as number), bottom: (ctx) => controlsBottom(ctx, spec, {focus: f, t})};
  });
  return tl;
};

export {fillRect};
