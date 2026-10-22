import {
  appQr,
  bottomBackground,
  button,
  drawAppQr,
  drawCentered,
  drawDots,
  drawMarquee,
  drawText,
  drawWrapped,
  fillRect,
  fillRound,
  fitEnd,
  focusRing,
  IconPainter,
  kindTile,
  lineHeight,
  panel,
  panelText,
  progressBar,
  recess,
  spinner,
  statusBar,
  strokeRound,
  tab,
  tagRow,
  textWidth,
  titleIcon,
  TOP_PANEL,
  topBackground,
  topChrome,
} from './draw';
import {drawIcon, IconName} from './icons';
import {
  centreOf,
  Control,
  controlsBottom,
  ControlsState,
  FocusTrack,
  ListSpec,
  ListState,
  listBottom,
  Painter,
  ScreenSpec,
  titleIconFramed,
} from './kit';
import {drawCamera, drawPhoto, ICONS} from './pictures';
import {APP_VERSION, C, Ctx, R, Rect, T} from './theme';
import {getClock, setClock} from './clock';
import {bytes, DESTINATION, Entry, FREE_SPACE, plural, Title} from './data';

// The app's screens, each a pair of painters built from a little state.

export type Shot = {top: Painter; bottom: Painter};

export {setClock, getClock};

export const chrome = (ctx: Ctx, title: string, icon?: IconName | null, color?: string, since = 0) => topChrome(ctx, getClock(), title, icon, color, since);

export const iconOf = (key: string): IconPainter => ICONS[key];

// --- Home ------------------------------------------------------------------------------------

export const homeSpec = (queueCount = 0, updateBadge = false): ScreenSpec => {
  const s = T.Small + 0.06;
  const controls: Control[] = [
    {rect: R(16, 6, 140, 26), label: 'Settings', icon: 'Wrench', iconColor: C.IconGrey, scale: s, badge: updateBadge ? 'New' : undefined},
    {rect: R(164, 6, 140, 26), label: 'Queue', icon: 'Download', iconColor: C.IconGreen, scale: s, badge: queueCount ? String(queueCount) : undefined},
    {rect: R(16, 42, 92, 78), label: 'Scan\nQR Code', icon: 'Scan', iconColor: C.IconBlue, tile: true, scale: s},
    {rect: R(114, 42, 92, 78), label: 'Enter\nURL', icon: 'Globe', iconColor: C.IconTeal, tile: true, scale: s},
    {rect: R(212, 42, 92, 78), label: 'Receive', icon: 'Chip', iconColor: C.IconPink, tile: true, scale: s},
    {rect: R(16, 127, 92, 76), label: 'History', icon: 'History', iconColor: C.IconRed, tile: true, scale: s},
    {rect: R(114, 127, 92, 76), label: 'Files', icon: 'Folder', iconColor: C.IconOrange, tile: true, scale: s},
    {rect: R(212, 127, 92, 76), label: 'Titles', icon: 'GameCard', iconColor: C.IconGold, tile: true, scale: s},
  ];
  return {controls, closeBar: true};
};

export const HOME = {settings: 0, queue: 1, scan: 2, enterUrl: 3, receive: 4, history: 5, files: 6, titles: 7} as const;

export const homeTop: Painter = (ctx) => {
  topBackground(ctx);
  statusBar(ctx, getClock());
  drawDots(ctx, 41);
  panel(ctx, R(17, 53, 366, 134));
  drawIcon(ctx, 'AppMark', 171, 60, 58, C.Accent);
  drawText(ctx, '3dsDL', 200, 116, T.Hero, C.Ink, 'center');
  drawMarquee(ctx, `Saves to ${DESTINATION}   ${FREE_SPACE}`, 200, 156, T.Small, C.InkSoft, 340, 'center');
  drawDots(ctx, 196);
  drawText(ctx, `Ver. ${APP_VERSION}`, 387, 210, 0.66, C.Heading, 'right');
};

export const home = (st: ControlsState, queueCount = 0): Shot => ({
  top: homeTop,
  bottom: (ctx) => controlsBottom(ctx, homeSpec(queueCount), st),
});

// --- Scan QR Code (ui::drawScanner) -------------------------------------------------------------

export const SCANNER_TYPE = R(36, 150, 248, 34);

export const scanner = (t: number, settle: number, status: string, warn = false): Shot => ({
  top: (ctx) => {
    fillRect(ctx, 0, 0, 400, 240, '#2A2E31');
    drawCamera(ctx, t, settle);
    const left = 130;
    const top = 46;
    const size = 140;
    const arm = 26;
    const line = 4;
    for (const pass of [0, 1]) {
      const o = pass === 0 ? 1 : 0;
      const fill = pass === 0 ? 'rgba(0,0,0,0.44)' : '#FFFFFF';
      fillRect(ctx, left + o, top + o, arm, line, fill);
      fillRect(ctx, left + o, top + o, line, arm, fill);
      fillRect(ctx, left + size - arm + o, top + o, arm, line, fill);
      fillRect(ctx, left + size - line + o, top + o, line, arm, fill);
      fillRect(ctx, left + o, top + size - line + o, arm, line, fill);
      fillRect(ctx, left + o, top + size - arm + o, line, arm, fill);
      fillRect(ctx, left + size - arm + o, top + size - line + o, arm, line, fill);
      fillRect(ctx, left + size - line + o, top + size - arm + o, line, arm, fill);
    }
    const g = ctx.createLinearGradient(0, 0, 0, 19);
    g.addColorStop(0, 'rgba(242,235,178,0.92)');
    g.addColorStop(1, 'rgba(242,233,163,0.92)');
    ctx.fillStyle = g;
    ctx.fillRect(0, 0, 400, 19);
    statusBar(ctx, getClock());
    fillRect(ctx, 0, 206, 400, 34, 'rgba(20,22,24,0.72)');
    drawCentered(ctx, status, R(10, 206, 380, 34), T.Body, warn ? '#FFD36B' : '#FFFFFF');
  },
  bottom: (ctx) => {
    bottomBackground(ctx);
    const title = 'Scan QR Code';
    const width = 26 + textWidth(title, 0.72);
    drawIcon(ctx, 'Scan', 160 - width / 2, 9, 20, C.Accent);
    drawText(ctx, title, 160 - width / 2 + 26, 19 - lineHeight(0.72) / 2 + 1, 0.72, C.Accent);
    const p = R(13, 44, 294, 90);
    panel(ctx, p);
    panelText(ctx, 'Point the outer camera at a QR code that holds a download link.', p);
    button(ctx, SCANNER_TYPE, 'Type a URL Instead', {scale: T.Body + 0.06});
    tab(ctx, 'left', 'Back');
  },
});

// --- Confirm download -------------------------------------------------------------------------------

export const confirmSpec = (folder: string): ScreenSpec => ({
  back: 'Cancel',
  action: 'Download',
  controls: [
    {rect: R(46, 124, 228, 34), label: 'Change Folder', icon: 'Folder', iconColor: C.IconOrange, scale: T.Body + 0.06},
    {rect: R(46, 166, 228, 34), label: 'Add to Queue', icon: 'Download', iconColor: C.IconGreen, scale: T.Body + 0.06},
  ],
  drawBottom: (ctx) => {
    drawText(ctx, 'Download this file?', 160, 14, T.Body, C.Ink, 'center');
    const box = R(13, 46, 294, 70);
    recess(ctx, box);
    drawText(ctx, 'Saves to', box.x + 12, box.y + 8, T.Small, C.InkSoft);
    drawMarquee(ctx, folder, box.x + 12, box.y + 32, T.Body, C.InkStrong, 270);
  },
});

// pulse > 0 glows the HTTPS row softly, for the feature panel.
export const confirmTop =
  (name: string, url: string, host: string, folder: string, pulse = 0): Painter =>
  (ctx) => {
    chrome(ctx, 'Download', 'Download', C.IconGreen);
    panel(ctx);
    const left = TOP_PANEL.x + 18;
    drawIcon(ctx, 'Package', left, TOP_PANEL.y + 14, 30, C.Accent);
    drawMarquee(ctx, name, left + 40, TOP_PANEL.y + 10, 0.62, C.InkStrong, 300);
    drawText(ctx, `from ${host}`, left + 40, TOP_PANEL.y + 36, T.Small, C.InkSoft);
    let y = drawWrapped(ctx, url, left, TOP_PANEL.y + 58, T.Tiny, C.InkSoft, TOP_PANEL.w - 36, 2) + 4;
    y = tagRow(ctx, left, y, 74, 'Save to', folder);
    if (pulse > 0) {
      ctx.save();
      ctx.shadowColor = `rgba(63,166,90,${0.75 * pulse})`;
      ctx.shadowBlur = 8 * pulse;
      fillRound(ctx, R(left, y, 74, 20), 5, C.Good);
      ctx.restore();
    }
    tagRow(ctx, left, y, 74, 'HTTPS', 'The certificate is checked.', C.Good);
  };

export const confirm = (st: ControlsState, name: string, url: string, host: string, folder = DESTINATION, pulse = 0): Shot => ({
  top: confirmTop(name, url, host, folder, pulse),
  bottom: (ctx) => controlsBottom(ctx, confirmSpec(folder), st),
});

// --- Progress screens -----------------------------------------------------------------------------

export const footer = 'Sleep Mode is paused until this finishes, so closing the system will not stop it.';

// --- CIA screens --------------------------------------------------------------------------------

export const ciaTop =
  (title: Title, header: string, icon: IconName, iconColor: string, versionRow: string, size: string): Painter =>
  (ctx) => {
    chrome(ctx, header, icon, iconColor);
    panel(ctx);
    const left = TOP_PANEL.x + 18;
    const top = TOP_PANEL.y + 12;
    titleIconFramed(ctx, left, top, iconOf(title.icon));
    drawMarquee(ctx, title.name, left + 60, top + 2, 0.62, C.InkStrong, 280);
    drawMarquee(ctx, title.publisher, left + 60, top + 30, T.Small, C.InkSoft, 280);
    let y = top + 60;
    y = tagRow(ctx, left, y, 76, 'Title ID', title.titleId);
    y = tagRow(ctx, left, y, 76, 'Version', versionRow);
    tagRow(ctx, left, y, 76, 'Size', size);
  };

export const installPromptSpec = (state: string): ScreenSpec => ({
  back: 'Keep File',
  controls: [
    {rect: R(26, 46, 268, 38), label: 'Install'},
    {rect: R(26, 97, 268, 38), label: 'Install and Delete File', scale: T.Body + 0.06},
  ],
  drawBottom: (ctx) => {
    drawText(ctx, 'Install this software?', 160, 13, T.Body, C.Ink, 'center');
    drawWrapped(ctx, `${state} To always do the same, see Settings, Downloads.`, 160, 148, T.Tiny, C.InkSoft, 270, 3, 'center');
  },
});

export const ciaOptionsSpec = (state: string): ScreenSpec => ({
  back: 'Back',
  controls: [
    {rect: R(26, 42, 268, 38), label: 'Install'},
    {rect: R(26, 90, 268, 38), label: 'Install and Delete File', scale: T.Body + 0.06},
    {rect: R(26, 140, 268, 26), label: 'Delete File', scale: T.Small + 0.06, labelColor: C.Bad},
  ],
  drawBottom: (ctx) => drawMarquee(ctx, state, 160, 12, T.Body, C.Ink, 300, 'center'),
});

// --- Files ----------------------------------------------------------------------------------------

export const kindOf = (e: Entry): {icon: IconName; color: string; label: string} => {
  if (e.folder) return {icon: 'Folder', color: C.IconOrange, label: 'Folder'};
  const ext = e.name.slice(e.name.lastIndexOf('.') + 1).toLowerCase();
  switch (ext) {
    case 'cia':
      return {icon: 'Package', color: C.IconBlue, label: 'CIA installer'};
    case '3dsx':
      return {icon: 'App', color: C.IconTeal, label: 'Homebrew app'};
    case 'jpg':
    case 'png':
    case 'bmp':
      return {icon: 'Picture', color: C.IconPink, label: 'Picture'};
    case 'txt':
    case 'ini':
      return {icon: 'Text', color: C.IconGrey, label: 'Text'};
    case 'zip':
      return {icon: 'Zip', color: C.IconGold, label: 'ZIP archive'};
    case 'bin':
    case 'smdh':
    case 'bcstm':
      return {icon: 'File', color: C.IconGrey, label: 'File'};
    default:
      return {icon: 'File', color: C.IconGrey, label: 'File'};
  }
};

export const filesSpec = (folder: string, entries: Entry[], atRoot: boolean): ListSpec => ({
  title: folder,
  items: entries.map((e) => {
    const k = kindOf(e);
    return {label: e.name, detail: e.folder ? '' : bytes(e.bytes ?? 0), icon: k.icon, iconColor: k.color};
  }),
  back: atRoot ? 'Close' : 'Back',
  action: 'Options',
  actionKey: 'X',
});

export const entryPanel = (
  ctx: Ctx,
  icon: {painter?: IconPainter; kind?: {icon: IconName; color: string}},
  name: string,
  second: string,
  tags: [string, string][],
) => {
  panel(ctx);
  const left = TOP_PANEL.x + 18;
  const top = TOP_PANEL.y + 12;
  if (icon.painter) titleIconFramed(ctx, left, top, icon.painter);
  else if (icon.kind) kindTile(ctx, left, top, icon.kind.icon, icon.kind.color);
  drawMarquee(ctx, name, left + 60, top + 2, 0.62, C.InkStrong, 290);
  drawMarquee(ctx, second, left + 60, top + 28, T.Tiny, C.InkSoft, 290);
  let y = top + 58;
  for (const [label, value] of tags) {
    if (!value || y > TOP_PANEL.y + TOP_PANEL.h - 24) continue;
    y = tagRow(ctx, left, y, 82, label, value);
  }
};

// Extra facts the Files top screen reads from a file.
export type EntryFacts = {
  cia?: {title: Title; versionRow: string};
  meta?: {title: Title};
  head?: string[];
  picture?: string;
};

export const filesTop =
  (folderTitle: string, folderPath: string, entry: Entry | null, facts: EntryFacts = {}): Painter =>
  (ctx) => {
    chrome(ctx, folderTitle, 'Folder', C.IconOrange);
    if (!entry) {
      panel(ctx);
      return;
    }
    const k = kindOf(entry);
    const path = folderPath + entry.name;
    if (entry.folder) {
      entryPanel(ctx, {kind: k}, entry.name, `${path}/`, [
        ['Type', 'Folder'],
        ['Contains', plural(entry.items ?? 0, 'item')],
        ['Changed', entry.changed],
      ]);
      return;
    }
    if (facts.cia) {
      const t = facts.cia.title;
      entryPanel(ctx, {painter: iconOf(t.icon)}, t.name, t.publisher, [
        ['Type', 'CIA, Game or app'],
        ['Version', facts.cia.versionRow],
        ['Size', bytes(t.bytes)],
      ]);
      return;
    }
    if (facts.meta) {
      const t = facts.meta.title;
      entryPanel(ctx, {painter: iconOf(t.icon)}, t.name, t.publisher, [
        ['Type', k.label],
        ['Size', bytes(entry.bytes ?? 0)],
      ]);
      return;
    }
    if (facts.head) {
      entryPanel(ctx, {kind: k}, entry.name, `${k.label}, ${bytes(entry.bytes ?? 0)}`, []);
      let y = TOP_PANEL.y + 70;
      for (const line of facts.head) {
        if (y > TOP_PANEL.y + TOP_PANEL.h - 16) break;
        drawText(ctx, fitEnd(line, T.Tiny, 330), TOP_PANEL.x + 18, y, T.Tiny, C.InkSoft);
        y += 13;
      }
      return;
    }
    entryPanel(ctx, {kind: k}, entry.name, k.label, [
      ['Picture', facts.picture ?? ''],
      ['Size', bytes(entry.bytes ?? 0)],
      ['Changed', entry.changed],
    ]);
  };

// --- Picture viewer -----------------------------------------------------------------------------

export const PICTURE_PREVIOUS = R(16, 116, 140, 40);
export const PICTURE_NEXT = R(164, 116, 140, 40);

export const pictureViewer = (photo: 'sunset' | 'aurora', name: string, facts: string, index: number, count: number, pressed: 'previous' | 'next' | 'back' | null): Shot => ({
  top: (ctx) => {
    fillRect(ctx, 0, 0, 400, 240, '#1A1C1E');
    drawPhoto(ctx, photo);
  },
  bottom: (ctx) => {
    bottomBackground(ctx);
    const width = 26 + Math.min(260, textWidth(name, 0.68));
    drawIcon(ctx, 'Picture', 160 - width / 2, 9, 20, C.IconPink);
    drawMarquee(ctx, name, 160 - width / 2 + 26, 9, 0.68, C.Ink, 260);
    const p = R(13, 44, 294, 54);
    panel(ctx, p);
    drawText(ctx, facts, 160, p.y + 7, T.Small, C.Ink, 'center');
    drawText(ctx, `Picture ${index + 1} of ${count} in this folder`, 160, p.y + 28, T.Small, C.InkSoft, 'center');
    button(ctx, PICTURE_PREVIOUS, 'Previous', {scale: T.Small + 0.02, pressed: pressed === 'previous'});
    button(ctx, PICTURE_NEXT, 'Next', {scale: T.Small + 0.02, pressed: pressed === 'next'});
    drawText(ctx, 'Swipe, or press L and R, for the others.', 160, 170, T.Tiny, C.InkSoft, 'center');
    tab(ctx, 'left', 'Back', '', false, pressed === 'back');
  },
});

// --- Text editor --------------------------------------------------------------------------------

export const EDITOR_EDIT = R(13, 92, 94, 34);
export const EDITOR_NEW = R(113, 92, 94, 34);
export const EDITOR_DELETE = R(213, 92, 94, 34);

const lineColor = (line: string) => {
  const s = line.trim();
  if (!s) return C.Ink;
  if (s[0] === '#' || s[0] === ';' || s.startsWith('//')) return C.InkSoft;
  if (s[0] === '[' && s[s.length - 1] === ']') return C.Accent;
  return C.Ink;
};

export const editorTop =
  (name: string, lines: string[], cursor: number, state: '' | 'edited' | 'saved', since = 0): Painter =>
  (ctx) => {
    topBackground(ctx);
    const paper = R(10, 50, 380, 184);
    const pitch = 16;
    fillRound(ctx, R(paper.x - 1, paper.y - 1, paper.w + 2, paper.h + 2), 7, '#CFC48A');
    fillRound(ctx, paper, 6, '#FFFDF4');
    fillRound(ctx, R(paper.x, paper.y, 30, paper.h), 6, '#EDE6C0', 1 | 4);
    lines.forEach((line, index) => {
      const y = paper.y + 2 + index * pitch;
      if (y > paper.y + paper.h) return;
      if (index === cursor) {
        fillRound(ctx, R(paper.x + 1, y, paper.w - 2, pitch), 2, [
          [0, '#D6EEF8'],
          [1, '#BFE3F3'],
        ]);
      }
      fillRect(ctx, paper.x + 30, y + pitch - 1, paper.w - 31, 1, '#E8E1BE');
      drawText(ctx, String(index + 1), paper.x + 26, y + 2, 0.3, '#A79D70', 'right');
      if (index === cursor) drawMarquee(ctx, line, paper.x + 36, y, 0.38, lineColor(line), paper.w - 44, 'left', since);
      else drawText(ctx, fitEnd(line, 0.38, paper.w - 44), paper.x + 36, y, 0.38, lineColor(line));
    });
    statusBar(ctx, getClock());
    const nameW = Math.min(260, textWidth(name, T.Title));
    const width = 28 + nameW + (state ? 8 + textWidth(state, T.Small) : 0);
    drawIcon(ctx, 'Text', 200 - width / 2, 22, 22, C.IconGrey);
    drawMarquee(ctx, name, 200 - width / 2 + 28, 21, T.Title, C.Accent, 260);
    if (state) drawText(ctx, state, 200 - width / 2 + 28 + nameW + 8, 26, T.Small, state === 'edited' ? '#E07A10' : C.InkSoft);
  };

export const editorBottom =
  (lines: string[], cursor: number, pressed: 'edit' | 'close' | 'save' | null, t: number): Painter =>
  (ctx) => {
    bottomBackground(ctx);
    const r = R(13, 8, 294, 72);
    recess(ctx, r);
    drawText(ctx, `Line ${cursor + 1} of ${lines.length}`, r.x + 10, r.y + 5, T.Tiny, C.InkSoft);
    const current = lines[cursor];
    drawWrapped(ctx, current || '(empty line)', r.x + 10, r.y + 24, T.Small, current ? C.InkStrong : C.InkSoft, r.w - 20, 2);
    button(ctx, EDITOR_EDIT, 'Edit', {scale: T.Small, pressed: pressed === 'edit'});
    button(ctx, EDITOR_NEW, 'New Line', {scale: T.Small});
    button(ctx, EDITOR_DELETE, 'Delete', {scale: T.Small});
    focusRing(ctx, EDITOR_EDIT, 9, t);
    drawText(ctx, 'A Edit   X New line   Y Delete', 160, 140, T.Tiny, C.InkSoft, 'center');
    drawText(ctx, 'Drag or flick here to scroll the page.', 160, 160, T.Tiny, C.Counter, 'center');
    tab(ctx, 'left', 'Close', '', false, pressed === 'close');
    tab(ctx, 'right', 'Save', 'START', false, pressed === 'save');
  };

// --- ZIP contents -------------------------------------------------------------------------------

export const zipSpec = (zip: {name: string; entries: {name: string; directory?: boolean; bytes: number}[]}): ListSpec => ({
  title: zip.name,
  items: zip.entries.map((e) => {
    const k = e.directory ? kindOf({name: e.name, folder: true, changed: ''}) : kindOf({name: e.name, changed: ''});
    return {label: e.name, detail: e.directory ? '' : bytes(e.bytes), icon: k.icon, iconColor: k.color};
  }),
  action: 'Extract',
  actionKey: 'X',
  emptyText: 'This archive is empty.',
});

export const zipTop =
  (zip: {name: string; fileCount: number; totalSize: number; entries: {name: string}[]}, index: number): Painter =>
  (ctx) => {
    chrome(ctx, zip.name, 'Zip', C.IconGold);
    panel(ctx);
    const left = TOP_PANEL.x + 18;
    let y = TOP_PANEL.y + 16;
    y = tagRow(ctx, left, y, 92, 'Holds', plural(zip.fileCount, 'file'));
    y = tagRow(ctx, left, y, 92, 'Extracted', bytes(zip.totalSize));
    if (index >= 0 && index < zip.entries.length) tagRow(ctx, left, y, 92, 'Selected', zip.entries[index].name);
    drawWrapped(ctx, 'Paths that would leave the folder are skipped.', left, TOP_PANEL.y + TOP_PANEL.h - 48, T.Small, C.InkSoft, TOP_PANEL.w - 36, 2);
  };

// --- Titles -------------------------------------------------------------------------------------

export const titlesSpec = (): ScreenSpec => {
  const s = T.Small + 0.06;
  return {
    back: 'Back',
    controls: [
      {rect: R(16, 6, 92, 26), label: 'Clean Up', scale: T.Small},
      {rect: R(114, 6, 92, 26), label: 'Tickets', scale: T.Small},
      {rect: R(212, 6, 92, 26), label: 'Seeds', scale: T.Small},
      {rect: R(16, 42, 140, 78), label: 'Software', icon: 'GameCard', iconColor: C.IconBlue, tile: true, scale: s},
      {rect: R(164, 42, 140, 78), label: 'Updates and\nDLC', icon: 'Download', iconColor: C.IconGreen, tile: true, scale: s},
      {rect: R(16, 127, 140, 76), label: 'Save Data', icon: 'Chip', iconColor: C.IconTeal, tile: true, scale: s},
      {rect: R(164, 127, 140, 76), label: 'Extra Data', icon: 'Folder', iconColor: C.IconOrange, tile: true, scale: s},
    ],
  };
};

const TITLES_HELP = [
  'Remove unfinished installs, which stop software from installing again, and tickets for software that is gone.',
  'Tickets let installed software start. Export them to the SD card, or delete those of software that is gone.',
  'Newer games need a seed to start. Import seeds from a seeddb.bin on the SD card for the games installed here.',
  'Games, apps, demos and DSiWare. Launch them, back up their saves, get a game’s seed, or delete them.',
  'Updates and add-on content, listed under their game’s name.',
  "Back up and restore game saves in Checkpoint's folders, so either app can restore the other's backups.",
  'Data that software keeps outside its save, such as downloaded content, courses or StreetPass data. Back it up, restore or delete it.',
];

export const titlesTop =
  (focus: number, counts: {software: number; sdBytes: number; tickets: number; addOns: number; extData: number}): Painter =>
  (ctx) => {
    chrome(ctx, 'Titles', 'GameCard', C.IconBlue);
    panel(ctx);
    const shown = focus >= 0 && focus < 7 ? focus : 3;
    const left = TOP_PANEL.x + 18;
    let y = drawWrapped(ctx, TITLES_HELP[shown].replace(/’/g, "'"), left, TOP_PANEL.y + 12, T.Small, C.Ink, TOP_PANEL.w - 36, 3) + 10;
    switch (shown) {
      case 0:
        y = tagRow(ctx, left, y, 110, 'Unfinished', plural(0, 'install'));
        tagRow(ctx, left, y, 110, 'Unused', plural(1, 'ticket'));
        break;
      case 1:
        y = tagRow(ctx, left, y, 110, 'Tickets', String(counts.tickets));
        tagRow(ctx, left, y, 110, 'Unused', '1');
        break;
      case 2:
        tagRow(ctx, left, y, 110, 'Looks in', 'sdmc:/seeddb.bin');
        break;
      case 3:
        y = tagRow(ctx, left, y, 110, 'Installed', plural(counts.software, 'title'));
        tagRow(ctx, left, y, 110, 'On SD card', bytes(counts.sdBytes));
        break;
      case 4:
        tagRow(ctx, left, y, 110, 'Installed', plural(counts.addOns, 'item'));
        break;
      case 5:
        tagRow(ctx, left, y, 110, 'Backups in', 'sdmc:/3ds/Checkpoint/saves/');
        break;
      default:
        y = tagRow(ctx, left, y, 110, 'On SD card', plural(counts.extData, 'item'));
        tagRow(ctx, left, y, 110, 'Backups in', 'sdmc:/3ds/Checkpoint/extdata/');
    }
  };

export const titleTop =
  (header: string, headerIcon: IconName, title: Title & {size: number}): Painter =>
  (ctx) => {
    chrome(ctx, header, headerIcon, C.IconBlue);
    panel(ctx);
    const left = TOP_PANEL.x + 18;
    const top = TOP_PANEL.y + 12;
    titleIconFramed(ctx, left, top, iconOf(title.icon));
    drawMarquee(ctx, title.name, left + 60, top + 2, 0.62, C.InkStrong, 280);
    drawMarquee(ctx, title.publisher, left + 60, top + 30, T.Small, C.InkSoft, 280);
    let y = top + 60;
    y = tagRow(ctx, left, y, 76, 'Title ID', title.titleId);
    y = tagRow(ctx, left, y, 76, 'Version', title.version);
    tagRow(ctx, left, y, 76, 'Size', `${bytes(title.size)}, SD card`);
  };

// --- Receive ----------------------------------------------------------------------------------------

export type Transfer = {upload: boolean; name: string; done: number; total: number; rate: number; from: string};

export const receiveTop =
  (page: string, ip: string, since = 0): Painter =>
  (ctx) => {
    chrome(ctx, 'Receive', 'Chip', C.IconPink);
    panel(ctx);
    const code = appQr(page);
    const px = code.size <= 25 ? 4 : 3;
    const side = code.size * px;
    const qrX = TOP_PANEL.x + 12 + 2 * px;
    const qrY = TOP_PANEL.y + (TOP_PANEL.h - side) / 2;
    drawAppQr(ctx, code, qrX, qrY, px);
    const left = qrX + side + 2 * px + 12;
    const width = TOP_PANEL.x + TOP_PANEL.w - 12 - left;
    drawWrapped(ctx, 'Scan this with a phone on the same Wi-Fi, or open the address below, to send links and manage files.', left, TOP_PANEL.y + 12, T.Small, C.Ink, width, 5, 'left', since);
    drawMarquee(ctx, `FBI's tools (servefiles.py, Boop): ${ip}, port 5000.`, left, TOP_PANEL.y + TOP_PANEL.h - 10 - lineHeight(T.Tiny), T.Tiny, C.InkSoft, width, 'left', since);
  };

export const receiveBottom =
  (page: string, named: string, last: string, transfer: Transfer | null, t: number, backPressed = false): Painter =>
  (ctx) => {
    bottomBackground(ctx);
    const p = R(13, 12, 294, 136);
    panel(ctx, p);
    if (transfer) {
      drawText(ctx, transfer.upload ? 'Receiving' : 'Sending', 160, p.y + 14, T.Small, C.InkSoft, 'center');
      drawMarquee(ctx, transfer.name, 160, p.y + 34, T.Body, C.Ink, 270, 'center');
      const fraction = transfer.total > 0 ? transfer.done / transfer.total : 1;
      progressBar(ctx, R(33, p.y + 64, 254, 14), fraction);
      drawText(ctx, `${bytes(transfer.done)} of ${bytes(transfer.total)}`, 160, p.y + 88, T.Small, C.Ink, 'center');
      const rateText = transfer.rate > 0 ? `${bytes(transfer.rate)}/s, ${transfer.upload ? 'from' : 'to'} ${transfer.from}` : `${transfer.upload ? 'From' : 'To'} ${transfer.from}`;
      drawText(ctx, rateText, 160, p.y + 110, T.Tiny, C.InkSoft, 'center');
    } else {
      spinner(ctx, 160, p.y + 24, 12, t);
      drawText(ctx, 'Waiting at', 160, p.y + 42, T.Body, C.Ink, 'center');
      drawMarquee(ctx, page, 160, p.y + 68, T.Body, C.Accent, 280, 'center');
      if (named) drawMarquee(ctx, `or ${named}`, 160, p.y + 92, T.Small, C.InkSoft, 280, 'center');
      drawMarquee(ctx, last ? `Last: ${last}` : 'Keep this screen open while sending.', 160, p.y + 114, T.Tiny, C.InkSoft, 280, 'center', t);
    }
    drawWrapped(ctx, `Links download into ${DESTINATION}. Files go where you put them on the page.`, 160, 160, T.Tiny, C.InkSoft, 270, 2, 'center', t);
    tab(ctx, 'left', 'Back', '', false, backPressed);
  };

// --- Settings ----------------------------------------------------------------------------------------

export const settingsSpec = (updateBadge = false): ScreenSpec => {
  const s = T.Small + 0.06;
  return {
    back: 'Back',
    action: 'Report Issue',
    controls: [
      {rect: R(16, 6, 140, 26), label: 'About', icon: 'AppMark', iconColor: C.Accent, scale: s},
      {rect: R(164, 6, 140, 26), label: 'Updates', icon: 'Download', iconColor: C.IconGreen, scale: s, badge: updateBadge ? 'New' : undefined},
      {rect: R(16, 42, 140, 78), label: 'Downloads', icon: 'Download', iconColor: C.IconGreen, tile: true, scale: s},
      {rect: R(164, 42, 140, 78), label: 'Download Folder', icon: 'Folder', iconColor: C.IconOrange, tile: true, scale: s},
      {rect: R(16, 127, 140, 76), label: 'Console Clock', icon: 'Clock', iconColor: C.IconBlue, tile: true, scale: s},
      {rect: R(164, 127, 140, 76), label: 'Other Settings', icon: 'Wrench', iconColor: C.IconGrey, tile: true, scale: s},
    ],
  };
};

const SETTINGS_HELP = [
  'Version, credits and where 3dsDL keeps its files.',
  'Check for a newer 3dsDL and install it.',
  'Resuming, Sleep Mode, ZIP files and what happens after a CIA downloads.',
  'The folder new downloads are saved to.',
  'Compare the console’s clock with the time online and set it. Secure downloads fail when the date is wrong.',
  'How Files sorts and shows files, sounds, and the download history.',
];

export const settingsTop =
  (focus: number | 'action' | 'back' | null): Painter =>
  (ctx) => {
    chrome(ctx, 'Settings', 'Wrench', C.IconGrey);
    panel(ctx);
    const left = TOP_PANEL.x + 18;
    if (focus === 'action') {
      drawWrapped(ctx, 'Where to report a bug, ask a question or reach the developer, as QR codes to scan with a phone.', left, TOP_PANEL.y + 12, T.Small, C.Ink, TOP_PANEL.w - 36, 3);
      return;
    }
    const shown = typeof focus === 'number' && focus >= 0 && focus < 6 ? focus : 2;
    let y = drawWrapped(ctx, SETTINGS_HELP[shown].replace(/’/g, "'"), left, TOP_PANEL.y + 12, T.Small, C.Ink, TOP_PANEL.w - 36, 3) + 10;
    if (shown === 1) {
      y = tagRow(ctx, left, y, 96, 'Installed', APP_VERSION, C.Tag);
      tagRow(ctx, left, y, 96, 'At Startup', 'Check');
    } else if (shown === 3) {
      tagRow(ctx, left, y, 96, 'Saves to', DESTINATION);
    } else if (shown === 4) {
      tagRow(ctx, left, y, 96, 'Console', '2026-10-20 20:16');
    } else if (shown === 5) {
      y = tagRow(ctx, left, y, 96, 'History', plural(6, 'download'));
      tagRow(ctx, left, y, 96, 'Sounds', 'On');
    } else if (shown === 2) {
      y = tagRow(ctx, left, y, 96, 'Resume', 'On');
      tagRow(ctx, left, y, 96, 'After a CIA', 'Ask');
    } else {
      tagRow(ctx, left, y, 96, 'Version', APP_VERSION);
    }
  };

export {centreOf, strokeRound};
export type {ListState, FocusTrack, Rect};
