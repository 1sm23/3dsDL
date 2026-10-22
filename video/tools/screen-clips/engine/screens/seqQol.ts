import {bytes, DEMO, DESTINATION, PACK_ZIP, plural, QUEUE_LINKS, rate, duration as fmtDuration} from './data';
import {drawCentered, notice, tagRow, TOP_PANEL} from './draw';
import {Timeline, TAP} from './engine';
import {
  controlsBottom,
  dialogBottom,
  dialogButtonRect,
  DialogSpec,
  dialogTop,
  focusAt,
  FocusId,
  listBottom,
  listMotion,
  ListSpec,
  progressBottom,
  ProgressView,
  rowRect,
  tabRect,
} from './kit';
import {
  chrome,
  ciaTop,
  confirm,
  footer,
  installPromptSpec,
  zipSpec,
  zipTop,
} from './screens';
import {downloadingView, progressShot} from './seqMain';
import {TITLE_SH23DS, bytes as fmtBytes} from './data';
import {drawIcon} from './icons';
import {drawMarquee, drawText, drawWrapped} from './text';
import {C, Ctx, easeInOut, span, T} from './theme';
import {panel} from './draw';

const at = (h: number, m: number, s = 0) => h * 3600 + m * 60 + s;

// --- Queue: three links and the notification -----------------------------------------------------

export const qolQueue = () => {
  const tl = new Timeline(2.6, at(19, 20, 10));
  const spec: ListSpec = {
    title: 'Queue',
    items: QUEUE_LINKS.map((l) => ({label: l.name, detail: l.host, icon: 'Download', iconColor: C.IconGreen})),
    action: 'Download All',
    actionKey: 'X',
  };
  const keys: [number, number][] = [
    [0, 0],
    [1.6, 1],
  ];
  tl.cue(1.6, 'move');
  tl.at(0, (t) => {
    const m = listMotion(spec, keys, t);
    return {
      top: (ctx) => {
        const item = QUEUE_LINKS[m.cursor];
        chrome(ctx, 'Queue', 'Download', C.IconGreen);
        panel(ctx);
        const left = TOP_PANEL.x + 18;
        drawIcon(ctx, 'Download', left, TOP_PANEL.y + 14, 30, C.IconGreen);
        drawMarquee(ctx, item.name, left + 40, TOP_PANEL.y + 10, 0.62, C.InkStrong, 300);
        drawText(ctx, `from ${item.host}`, left + 40, TOP_PANEL.y + 36, T.Small, C.InkSoft);
        let y = drawWrapped(ctx, item.url, left, TOP_PANEL.y + 58, T.Tiny, C.InkSoft, TOP_PANEL.w - 36, 2) + 4;
        y = tagRow(ctx, left, y, 74, 'Save to', DESTINATION);
        tagRow(ctx, left, y, 74, 'Position', `${m.cursor + 1} of ${QUEUE_LINKS.length}`);
      },
      bottom: (ctx) => listBottom(ctx, spec, {...m, t}),
    };
  });
  tl.cue(0.5, 'notify');
  tl.overlay((top, _bottom, t) =>
    notice(top, {title: 'Added to the Queue', text: '3 waiting in Queue on Home.', icon: 'Download', color: C.IconGreen}, t - 0.5),
  );
  return tl;
};

// --- Resume: the dropped connection, Retry, and on from 46% ----------------------------------------------

const KEPT = Math.round(DEMO.bytes * 0.46);

export const FAILED_DIALOG: DialogSpec = {
  header: 'Download',
  title: 'The download could not be completed.',
  tone: 'error',
  body: [
    {text: 'The connection dropped before the download finished.', maxLines: 4},
    {text: `${bytes(KEPT)} arrived before it stopped. Retrying continues from there.`, color: C.Good, maxLines: 2},
    {text: `transfer closed with ${DEMO.bytes - KEPT} bytes remaining to read`, color: C.InkSoft, maxLines: 2},
  ],
  buttons: ['Retry', 'Close'],
};

export const qolResume = () => {
  const tl = new Timeline(2.6, at(15, 52, 20));
  const tapAt = 1.2;
  const retry = dialogButtonRect(FAILED_DIALOG, 0);
  tl.tapRect(tapAt, retry);
  const under = downloadingView(0.46, 3, false, true);
  tl.at(0, (t, local) => ({
    top: (ctx) => dialogTop(ctx, FAILED_DIALOG),
    bottom: (ctx) =>
      dialogBottom(ctx, FAILED_DIALOG, {selected: 0, pressed: Timeline.pressed(t, tapAt) ? 0 : -1, age: local + 1, t}, (c) =>
        progressBottom(c, under, t),
      ),
  }));
  tl.cue(tapAt + TAP, 'select');
  const resumed = tapAt + TAP + 0.12;
  tl.at(tapAt + TAP, (t) =>
    progressShot({...downloadingView(0.46, t, true, false), title: 'Connecting', fraction: -1, amounts: bytes(0), speed: '', name: DEMO.host}, t),
  );
  tl.at(resumed, (t) => {
    const f = 0.46 + 0.18 * easeInOut(span(t, resumed, 2.6));
    const v = downloadingView(f, t, t < resumed + 0.35, true);
    return progressShot({...v, note: `Continuing from ${bytes(KEPT)} downloaded earlier`}, t);
  });
  return tl;
};

// --- File exists: Keep Both or Replace -----------------------------------------------------------------

export const EXISTS_DIALOG: DialogSpec = {
  header: 'Download',
  title: 'A file with this name already exists. What would you like to do?',
  tone: 'warning',
  body: [
    {text: `${DEMO.name} is already in ${DESTINATION}.`, maxLines: 2},
    {text: 'Keep Both saves the new file as sh23ds (1).cia.', color: C.InkSoft, maxLines: 3},
    {text: 'Replace swaps the old file for the new one.', color: C.InkSoft, maxLines: 2},
  ],
  buttons: ['Keep Both', 'Replace', 'Cancel'],
  dangerous: 1,
};

export const qolExists = () => {
  const tl = new Timeline(2.6, at(15, 38, 30));
  const keys: [number, number][] = [
    [0, 0],
    [0.7, 1],
    [1.3, 0],
  ];
  tl.cue(0.7, 'move').cue(1.3, 'move');
  const tapAt = 2.0;
  tl.tapRect(tapAt, dialogButtonRect(EXISTS_DIALOG, 0));
  const under = downloadingView(1, 3, false, true);
  tl.at(0, (t, local) => ({
    top: (ctx) => dialogTop(ctx, EXISTS_DIALOG),
    bottom: (ctx) =>
      dialogBottom(ctx, EXISTS_DIALOG, {selected: focusAt(keys, t).id, pressed: Timeline.pressed(t, tapAt) ? 0 : -1, age: local + 1, t}, (c) =>
        progressBottom(c, under, t),
      ),
  }));
  tl.cue(tapAt + TAP, 'select');
  const spec = installPromptSpec('Not installed yet.');
  const top = ciaTop(TITLE_SH23DS, 'Download Complete', 'Download', C.IconGreen, '0.2.2, not installed', `${fmtBytes(TITLE_SH23DS.bytes)}, SD card`);
  tl.at(tapAt + TAP + 0.05, (t) => ({top, bottom: (ctx) => controlsBottom(ctx, spec, {focus: focusAt<FocusId>([[0, 0]], t), t})}));
  return tl;
};

// --- Download folder presets ---------------------------------------------------------------------------------

const PRESETS: [string, string, boolean][] = [
  ['SD card root', 'sdmc:/', true],
  ['Downloads', 'sdmc:/Downloads/', true],
  ['CIA installers', 'sdmc:/cias/', true],
  ['Homebrew apps', 'sdmc:/3ds/', true],
  ['Luma3DS payloads', 'sdmc:/luma/payloads/', true],
  ['GodMode9 scripts', 'sdmc:/gm9/scripts/', false],
];

export const folderSpec = (): ListSpec => ({
  title: 'Download Folder',
  items: [
    {label: 'Browse the SD Card...', labelColor: C.Accent, icon: 'Folder', iconColor: C.IconOrange},
    {label: 'Type a Folder...', labelColor: C.Accent, icon: 'Text', iconColor: C.IconGrey},
    ...PRESETS.map(([label, path, exists]) => (exists ? {label, detail: path} : {label, detail: path, badge: 'New', badgeColor: C.Tag})),
  ],
});

export const folderTop = (index: number) => (ctx: Ctx) => {
  chrome(ctx, 'Download Folder', 'Folder', C.IconOrange);
  panel(ctx);
  const left = TOP_PANEL.x + 18;
  let y = TOP_PANEL.y + 20;
  y = tagRow(ctx, left, y, 82, 'Now', DESTINATION);
  let note = '';
  if (index === 0) note = 'Pick any folder on the SD card, or make a new one.';
  else if (index === 1) note = 'Type a path that starts with sdmc:/.';
  else {
    const [, path, exists] = PRESETS[index - 2];
    y = tagRow(ctx, left, y, 82, 'Selected', path, C.Good);
    note = exists ? 'New downloads will be saved here.' : 'It will be created when you choose it.';
  }
  drawWrapped(ctx, note, 200, y + 10, T.Small, C.InkSoft, 320, 2, 'center');
};

export const qolFolders = () => {
  const tl = new Timeline(2.6, at(15, 51, 5));
  const spec = folderSpec();
  const keys: [number, number][] = [
    [0, 2],
    [0.55, 3],
    [1.05, 4],
    [1.55, 5],
    [2.05, 6],
  ];
  for (const [t] of keys.slice(1)) tl.cue(t, 'move');
  tl.at(0, (t) => {
    const m = listMotion(spec, keys, t);
    return {top: folderTop(m.cursor), bottom: (ctx) => listBottom(ctx, spec, {...m, t})};
  });
  return tl;
};

// --- ZIP: contents, Extract, Here, a quick extraction ------------------------------------------------------------

export const extractDialog = (zipName: string, fileCount: number, totalSize: number, folder: string, stem: string): DialogSpec => ({
  header: 'extract',
  title: `Extract ${zipName}?`,
  tone: 'success',
  body: [
    {
      text: `It holds ${plural(fileCount, 'file')} (${bytes(totalSize)}). Here extracts into ${folder}, New Folder into ${folder}${stem}/.`,
      maxLines: 5,
    },
  ],
  buttons: ['Here', 'New Folder', 'Not Now'],
});

export const extractedDialog = (fileCount: number, folder: string): DialogSpec => ({
  header: 'done',
  title: 'The archive has been extracted.',
  tone: 'success',
  body: [{text: `${plural(fileCount, 'file')} extracted into ${folder}.`, maxLines: 3}],
  buttons: ['Done', 'Delete ZIP'],
  dangerous: 1,
});

export const extractingView = (zip: {name: string; fileCount: number; totalSize: number; entries: {name: string; directory?: boolean; bytes: number}[]}, folder: string, fraction: number): ProgressView => {
  const done = Math.round(zip.totalSize * fraction);
  let acc = 0;
  let current = zip.entries[0].name;
  for (const e of zip.entries) {
    if (e.directory) continue;
    current = e.name;
    acc += e.bytes;
    if (acc >= done) break;
  }
  return {
    title: 'Extracting',
    icon: 'Zip',
    iconColor: C.IconGold,
    name: zip.name,
    source: current,
    fraction,
    amounts: `${bytes(done)} of ${bytes(zip.totalSize)}`,
    speed: '',
    tag: `${zip.fileCount} files`,
    message: `Extracting into ${folder}`,
    detail: footer,
  };
};

export const qolZip = () => {
  const tl = new Timeline(2.6, at(15, 53, 10));
  const spec = zipSpec(PACK_ZIP);
  const top = zipTop(PACK_ZIP, 0);
  const tapExtract = 0.5;
  const ask = extractDialog(PACK_ZIP.name, PACK_ZIP.fileCount, PACK_ZIP.totalSize, DESTINATION, 'homebrew-pack');
  const done = extractedDialog(PACK_ZIP.fileCount, DESTINATION);
  const ext = tabRect('right');
  tl.tap(tapExtract, ext.x + ext.w / 2 + 6, ext.y + ext.h / 2 + 2);
  const listAt = (t: number) => (ctx: Ctx) => listBottom(ctx, spec, {cursor: 0, t, pressed: Timeline.pressed(t, tapExtract) ? 'action' : null});
  tl.at(0, (t) => ({top, bottom: listAt(t)}));
  const askAt = tapExtract + TAP;
  const tapHere = 1.6;
  tl.tapRect(tapHere, dialogButtonRect(ask, 0));
  tl.at(
    askAt,
    (t, local) => ({
      top: (ctx) => dialogTop(ctx, ask),
      bottom: (ctx) => dialogBottom(ctx, ask, {selected: 0, pressed: Timeline.pressed(t, tapHere) ? 0 : -1, age: local, t}, listAt(askAt)),
    }),
    0,
  );
  tl.cue(tapHere + TAP, 'select');
  const startX = tapHere + TAP;
  const endX = 2.3;
  tl.at(startX, (t) => progressShot(extractingView(PACK_ZIP, DESTINATION, easeInOut(span(t, startX + 0.05, endX))), t));
  const last = extractingView(PACK_ZIP, DESTINATION, 1);
  tl.at(
    endX + 0.05,
    (t, local) => ({
      top: (ctx) => dialogTop(ctx, done),
      bottom: (ctx) => dialogBottom(ctx, done, {selected: 0, age: local, t}, (c) => progressBottom(c, last, t)),
    }),
    0,
  );
  tl.cue(endX + 0.05, 'done');
  return tl;
};

// --- HTTPS: the certificate is checked ------------------------------------------------------------------------------

export const qolHttps = () => {
  const tl = new Timeline(2.6, at(19, 31, 0));
  const url = 'https://github.com/astronautlevel2/Anemone3DS/releases/latest/download/Anemone3DS.cia';
  tl.at(0, (t) => {
    const pulse = 0.5 + 0.5 * Math.sin((t - 0.3) * Math.PI * 1.6);
    return confirm({focus: focusAt<FocusId>([[0, 'action']], t), t}, 'Anemone3DS.cia', url, 'github.com', DESTINATION, t < 0.3 ? 0 : pulse);
  });
  return tl;
};

export {drawCentered, rowRect, rate, fmtDuration};
