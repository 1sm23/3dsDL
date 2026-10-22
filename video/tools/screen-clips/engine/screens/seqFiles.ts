import {
  bytes,
  Entry,
  NOTES_BEFORE,
  NOTES_EDIT_LINE,
  NOTES_TYPED,
  PICTURES,
  ROOT_AFTER_EXTRACT,
  ROOT_ENTRIES,
  THEMES_ZIP,
  TITLE_3DSFM,
  TITLE_CHECKPOINT,
  rate,
  duration as fmtDuration,
} from './data';
import {button, drawCentered, panel, panelText, tab} from './draw';
import {Timeline, TAP} from './engine';
import {
  busyBottom,
  busyTop,
  controlsBottom,
  dialogBottom,
  dialogButtonRect,
  DialogSpec,
  dialogTop,
  focusAt,
  FocusId,
  KEYBOARD_OK,
  keyboardBottom,
  keyboardTop,
  keyRect,
  ListSpec,
  listBottom,
  listMotion,
  Painter,
  progressBottom,
  ProgressView,
  rowRect,
  ScreenSpec,
  tabRect,
} from './kit';
import {
  chrome,
  ciaOptionsSpec,
  ciaTop,
  editorBottom,
  editorTop,
  EDITOR_EDIT,
  EntryFacts,
  entryPanel,
  filesSpec,
  filesTop,
  iconOf,
  PICTURE_NEXT,
  pictureViewer,
  Shot,
  zipSpec,
  zipTop,
} from './screens';
import {extractDialog, extractedDialog, extractingView} from './seqQol';
import {progressShot} from './seqMain';
import {BOTTOM_W, C, Ctx, easeInOut, R, span, T, TOP_W} from './theme';

const at = (h: number, m: number, s = 0) => h * 3600 + m * 60 + s;

const NOTES_AFTER = NOTES_BEFORE.map((line, i) => (i === NOTES_EDIT_LINE ? line + NOTES_TYPED : line));

const factsFor = (e: Entry, edited: boolean): EntryFacts => {
  if (e.name === 'Checkpoint.cia') return {cia: {title: TITLE_CHECKPOINT, versionRow: '3.10.1, not installed'}};
  if (e.name === '3dsFM.3dsx') return {meta: {title: TITLE_3DSFM}};
  if (e.name === 'notes.txt') return {head: edited ? NOTES_AFTER : NOTES_BEFORE};
  const pic = PICTURES.find((p) => p.name === e.name);
  if (pic) return {picture: `${pic.width} x ${pic.height} pixels`};
  return {};
};

// A Files folder: the list at the bottom, the highlighted entry on top. A
// row being touched shows pressed and the ring waits for the stylus to lift.
const folderShot = (
  folder: string,
  entries: Entry[],
  keys: [number, number][],
  t: number,
  opts: {heldRow?: number; heldAt?: number; tabAt?: number; tabSide?: 'back' | 'action'} = {},
): Shot => {
  const atRoot = folder === 'sdmc:/';
  if (t >= FILES.tapSave) {
    entries = entries.map((e) => (e.name === 'notes.txt' ? {...e, bytes: (e.bytes ?? 0) + NOTES_TYPED.length, changed: '2026-10-20 20:14'} : e));
  }
  const spec = filesSpec(folder, entries, atRoot);
  const m = listMotion(spec, keys, t);
  const held = opts.heldAt != null && Timeline.pressed(t, opts.heldAt) ? opts.heldRow : undefined;
  const tabDown = opts.tabAt != null && Timeline.pressed(t, opts.tabAt) ? opts.tabSide ?? 'back' : null;
  const title = atRoot ? 'SD Card' : folder.replace(/\/$/, '').split('/').pop()!;
  const edited = t >= FILES.tapSave;
  const entry = entries[m.cursor] ?? null;
  return {
    top: filesTop(title, folder, entry, entry ? factsFor(entry, edited) : {}),
    bottom: (ctx) => listBottom(ctx, spec, {...m, held, ring: held == null, pressed: tabDown, t}),
  };
};

const rowTap = (folder: string, entries: Entry[], keys: [number, number][], index: number, t: number) => {
  const spec = filesSpec(folder, entries, folder === 'sdmc:/');
  const m = listMotion(spec, keys, t);
  return rowRect(spec, index, m.offset);
};

const busyShot = (under: Shot, header: string, title: string, t: number, local: number): Shot => ({
  top: (ctx) => busyTop(ctx, header, title, under.top),
  bottom: (ctx) => busyBottom(ctx, under.bottom, local, t),
});

const dialogShot = (d: DialogSpec, under: Painter, selected: number, pressed: number, t: number, local: number): Shot => ({
  top: (ctx) => dialogTop(ctx, d),
  bottom: (ctx) => dialogBottom(ctx, d, {selected, pressed, age: local, t}, under),
});

export const FILES = {
  tapPictures: 2.2,
  tapPicture: 2.65,
  viewer: 2.95,
  tapNext: 4.1,
  viewer2: 4.4,
  tapBackViewer: 4.6,
  tapBackFolder: 4.85,
  root2: 5.0,
  pressA: 5.62,
  editor: 5.65,
  tapEdit: 6.35,
  keyboard: 6.5,
  typing: 6.65,
  tapOk: 7.75,
  edited: 7.9,
  tapSave: 8.1,
  tapClose: 8.35,
  root3: 8.45,
  tapZip: 8.7,
  zip: 9.0,
  tapExtract: 9.6,
  tapHere: 10.05,
  extracted: 10.65,
  tapDone: 10.85,
  root4: 11.0,
  tapCia: 11.2,
  tapInstall: 11.8,
  installedDialog: 13.2,
  tapCloseDialog: 13.32,
  root5: 13.5,
  tap3dsx: 13.75,
  tapStart: 14.8,
  dark: 15.6,
};

export const files = () => {
  const F = FILES;
  const tl = new Timeline(16.0, at(20, 14, 20));

  // F0: the SD card, the ring down to pictures, tap.
  const keys0: [number, number][] = [
    [0, 0],
    [0.7, 1],
    [1.05, 2],
    [1.4, 3],
    [1.75, 4],
  ];
  for (const [t] of keys0.slice(1)) tl.cue(t, 'move');
  tl.tapRect(F.tapPictures, rowTap('sdmc:/', ROOT_ENTRIES, keys0, 4, F.tapPictures));
  tl.at(0, (t) => folderShot('sdmc:/', ROOT_ENTRIES, keys0, t, {heldRow: 4, heldAt: F.tapPictures}));
  tl.cue(F.tapPictures + TAP, 'select');

  // F1: pictures, the viewer, Next, back out.
  const picEntries: Entry[] = PICTURES;
  const picKeys: [number, number][] = [[0, 0]];
  tl.tapRect(F.tapPicture, rowTap('sdmc:/pictures/', picEntries, picKeys, 0, F.tapPicture));
  const pictureList = (t: number, opts = {}) => folderShot('sdmc:/pictures/', picEntries, picKeys, t, {heldRow: 0, heldAt: F.tapPicture, ...opts});
  tl.at(F.tapPictures + TAP, (t) => pictureList(t));
  tl.cue(F.tapPicture + TAP, 'select');
  tl.at(F.tapPicture + TAP, (t, local) => busyShot(pictureList(F.tapPicture + TAP), 'Pictures', '', t, local), 0);
  const facts = (i: number) => `${PICTURES[i].width} x ${PICTURES[i].height} pixels, ${bytes(PICTURES[i].bytes!)}`;
  tl.tapRect(F.tapNext, PICTURE_NEXT);
  const viewer1 = (t: number) => pictureViewer('sunset', PICTURES[0].name, facts(0), 0, 2, Timeline.pressed(t, F.tapNext) ? 'next' : null);
  tl.at(F.viewer, (t) => viewer1(t), 0.08);
  tl.at(F.tapNext + TAP, (t, local) => busyShot(viewer1(F.tapNext + TAP), 'Pictures', '', t, local), 0);
  const back = tabRect('left');
  tl.tapRect(F.tapBackViewer, back);
  tl.at(F.viewer2, (t) => pictureViewer('aurora', PICTURES[1].name, facts(1), 1, 2, Timeline.pressed(t, F.tapBackViewer) ? 'back' : null), 0.08);
  tl.cue(F.tapBackViewer + TAP, 'back');
  tl.tapRect(F.tapBackFolder, back);
  tl.at(F.tapBackViewer + TAP, (t) => folderShot('sdmc:/pictures/', picEntries, [[0, 0]], t, {tabAt: F.tapBackFolder}));
  tl.cue(F.tapBackFolder + TAP, 'back');

  // F2: back on the SD card, down to notes.txt, the editor, a typed edit.
  const keys2: [number, number][] = [
    [F.root2, 4],
    [5.15, 5],
    [5.3, 6],
    [5.45, 7],
  ];
  for (const [t] of keys2.slice(1)) tl.cue(t, 'move');
  tl.at(F.root2, (t) => folderShot('sdmc:/', ROOT_ENTRIES, keys2, t));
  tl.cue(F.pressA, 'select');
  const lines0 = NOTES_BEFORE;
  const edited = [...lines0];
  edited[NOTES_EDIT_LINE] = lines0[NOTES_EDIT_LINE] + NOTES_TYPED;
  const cursorKeys: [number, number][] = [
    [F.editor, 0],
    [5.85, 1],
    [6.0, 2],
    [6.15, NOTES_EDIT_LINE],
  ];
  for (const [t] of cursorKeys.slice(1)) tl.cue(t, 'move');
  tl.tapRect(F.tapEdit, EDITOR_EDIT);
  const editorAt = (t: number, lines: string[], state: '' | 'edited' | 'saved', pressed: 'edit' | 'close' | 'save' | null): Shot => {
    const cursor = focusAt(cursorKeys, t).id;
    return {top: editorTop('notes.txt', lines, cursor, state), bottom: editorBottom(lines, cursor, pressed, t)};
  };
  tl.at(F.editor, (t) => editorAt(t, lines0, '', Timeline.pressed(t, F.tapEdit) ? 'edit' : null));
  tl.cue(F.tapEdit + TAP, 'select');
  // The keyboard: the line so far, one key at a time.
  const typedTimes = Array.from(NOTES_TYPED).map((ch, i) => ({ch, t: F.typing + i / 12}));
  for (const k of typedTimes) {
    const r = keyRect(k.ch)!;
    tl.tap(k.t, r.x + r.w / 2, r.y + r.h / 2, 0.06);
    tl.cue(k.t, 'type');
  }
  tl.tapRect(F.tapOk, KEYBOARD_OK);
  const editorUnder = editorAt(F.tapEdit + TAP, lines0, '', null);
  tl.at(
    F.keyboard,
    (t, local) => {
      const typed = lines0[NOTES_EDIT_LINE] + typedTimes.filter((k) => k.t <= t).map((k) => k.ch).join('');
      const down = typedTimes.find((k) => t >= k.t && t < k.t + 0.06);
      return {
        top: (ctx) => keyboardTop(ctx, `Line ${NOTES_EDIT_LINE + 1}`, typed, editorUnder.top, local, t),
        bottom: (ctx) => {
          editorUnder.bottom(ctx);
          keyboardBottom(ctx, down ? down.ch : null, Timeline.pressed(t, F.tapOk), local, typed);
        },
      };
    },
    0,
  );
  tl.cue(F.tapOk + TAP, 'select');
  const save = tabRect('right');
  tl.tap(F.tapSave, save.x + save.w / 2 + 6, save.y + save.h / 2 + 2);
  tl.at(F.edited, (t) => editorAt(t, edited, t >= F.tapSave + TAP ? 'saved' : 'edited', Timeline.pressed(t, F.tapSave) ? 'save' : null), 0.06);
  tl.cue(F.tapSave + TAP, 'select');
  tl.tapRect(F.tapClose, back, 0.1);
  tl.at(F.tapSave + TAP, (t) => editorAt(t, edited, 'saved', Timeline.pressed(t, F.tapClose, 0.1) ? 'close' : null), 0);
  tl.cue(F.tapClose + 0.1, 'back');

  // F3: themes.zip, Extract, Here, done.
  const keys3: [number, number][] = [[F.root3, 7]];
  tl.tapRect(F.tapZip, rowTap('sdmc:/', ROOT_ENTRIES, keys3, 8, F.tapZip));
  const root3 = (t: number) => folderShot('sdmc:/', ROOT_ENTRIES, keys3, t, {heldRow: 8, heldAt: F.tapZip});
  tl.at(F.root3, (t) => root3(t), 0.08);
  tl.cue(F.tapZip + TAP, 'select');
  tl.at(F.tapZip + TAP, (t, local) => busyShot(root3(F.tapZip + TAP), 'ZIP Archive', 'Reading themes.zip...', t, local), 0);
  const zSpec = zipSpec(THEMES_ZIP);
  const zTop = zipTop(THEMES_ZIP, 0);
  const ext = tabRect('right');
  tl.tap(F.tapExtract, ext.x + ext.w / 2 + 6, ext.y + ext.h / 2 + 2);
  const zipBottom = (t: number) => (ctx: Ctx) => listBottom(ctx, zSpec, {cursor: 0, t, pressed: Timeline.pressed(t, F.tapExtract) ? 'action' : null});
  tl.at(F.zip, (t) => ({top: zTop, bottom: zipBottom(t)}), 0.08);
  const ask = extractDialog(THEMES_ZIP.name, THEMES_ZIP.fileCount, THEMES_ZIP.totalSize, 'sdmc:/', 'themes');
  tl.tapRect(F.tapHere, dialogButtonRect(ask, 0));
  tl.at(F.tapExtract + TAP, (t, local) => dialogShot(ask, zipBottom(F.tapExtract + TAP), 0, Timeline.pressed(t, F.tapHere) ? 0 : -1, t, local), 0);
  tl.cue(F.tapHere + TAP, 'select');
  const xStart = F.tapHere + TAP;
  tl.at(xStart, (t) => progressShot(extractingView(THEMES_ZIP, 'sdmc:/', easeInOut(span(t, xStart + 0.04, F.extracted - 0.05))), t));
  const done = extractedDialog(THEMES_ZIP.fileCount, 'sdmc:/');
  const lastX = extractingView(THEMES_ZIP, 'sdmc:/', 1);
  tl.tapRect(F.tapDone, dialogButtonRect(done, 0));
  tl.at(F.extracted, (t, local) => dialogShot(done, (c) => progressBottom(c, lastX, t), 0, Timeline.pressed(t, F.tapDone) ? 0 : -1, t, local), 0);
  tl.cue(F.extracted, 'done');
  tl.cue(F.tapDone + TAP, 'select');

  // F4: Checkpoint.cia, Install, installed.
  const keys4: [number, number][] = [[F.root4, 9]];
  tl.tapRect(F.tapCia, rowTap('sdmc:/', ROOT_AFTER_EXTRACT, keys4, 7, F.tapCia));
  tl.at(F.root4, (t) => folderShot('sdmc:/', ROOT_AFTER_EXTRACT, keys4, t, {heldRow: 7, heldAt: F.tapCia}));
  tl.cue(F.tapCia + TAP, 'select');
  const ciaSpec = ciaOptionsSpec('Not installed yet.');
  const ciaTopP = ciaTop(TITLE_CHECKPOINT, 'CIA Installer', 'Package', C.IconBlue, '3.10.1, not installed', `${bytes(TITLE_CHECKPOINT.bytes)}, SD card`);
  tl.tapRect(F.tapInstall, ciaSpec.controls[0].rect);
  tl.at(F.tapCia + TAP, (t) => ({
    top: ciaTopP,
    bottom: (ctx) => controlsBottom(ctx, ciaSpec, {focus: focusAt<FocusId>([[0, 0]], t), pressed: Timeline.pressed(t, F.tapInstall) ? 0 : null, t}),
  }));
  tl.cue(F.tapInstall + TAP, 'select');
  const iStart = F.tapInstall + TAP;
  const installing = (f: number, t: number): ProgressView => {
    const total = TITLE_CHECKPOINT.bytes;
    const doneB = Math.round(total * f);
    const speed = 1024 * 1024 * (2.2 + 0.06 * Math.sin(Math.floor(t * 4) * 1.3));
    return {
      title: 'Installing',
      icon: 'Package',
      iconColor: C.IconBlue,
      name: 'Checkpoint',
      source: 'Checkpoint.cia',
      titleIcon: iconOf('Checkpoint'),
      fraction: f,
      amounts: `${bytes(doneB)} of ${bytes(total)}`,
      speed: f < 0.1 ? '' : `${rate(speed)}, ${fmtDuration((total - doneB) / speed)} left`,
      message: 'Do not turn off the power or remove the SD card.',
      detail: 'Installing from sdmc:/Checkpoint.cia',
    };
  };
  tl.at(iStart, (t) => progressShot(installing(easeInOut(span(t, iStart + 0.08, F.installedDialog - 0.1)), t), t));
  const installed: DialogSpec = {
    header: 'Installation Complete',
    title: 'Checkpoint has been installed. You can find it on the HOME Menu.',
    tone: 'success',
    icon: iconOf('Checkpoint'),
    body: [
      {text: 'Checkpoint', maxLines: 1},
      {text: `Game or app, 3.10.1 (v3745), ${bytes(TITLE_CHECKPOINT.bytes)}, installed in 1s`, color: C.InkSoft, maxLines: 2},
      {text: 'The CIA file is still at sdmc:/Checkpoint.cia.', color: C.InkSoft, maxLines: 2},
    ],
    buttons: ['Close', 'Launch'],
  };
  const lastI = installing(1, F.installedDialog);
  tl.tapRect(F.tapCloseDialog, dialogButtonRect(installed, 0));
  tl.at(F.installedDialog, (t, local) => dialogShot(installed, (c) => progressBottom(c, lastI, t), 1, Timeline.pressed(t, F.tapCloseDialog) ? 0 : -1, t, local), 0);
  tl.cue(F.installedDialog, 'installed');
  tl.cue(F.tapCloseDialog + TAP, 'back');

  // F5: 3dsFM.3dsx, Start, and 3dsDL hands over.
  const keys5: [number, number][] = [[F.root5 - 0.03, 7]];
  tl.tapRect(F.tap3dsx, rowTap('sdmc:/', ROOT_AFTER_EXTRACT, keys5, 6, F.tap3dsx));
  tl.at(F.tapCloseDialog + TAP, (t) => folderShot('sdmc:/', ROOT_AFTER_EXTRACT, keys5, t, {heldRow: 6, heldAt: F.tap3dsx}));
  tl.cue(F.tap3dsx + TAP, 'select');
  const hbSpec: ScreenSpec = {
    back: 'Back',
    controls: [
      {rect: R(46, 150, 228, 34), label: 'Open as Hex', icon: 'Hex', iconColor: C.IconGrey, scale: T.Body + 0.06},
      {rect: R(46, 106, 228, 34), label: 'Start', icon: 'App', iconColor: C.IconGreen, scale: T.Body + 0.06},
    ],
    drawBottom: (ctx) => {
      const p = R(13, 16, 294, 76);
      panel(ctx, p);
      panelText(ctx, 'Start closes 3dsDL and opens it.', p, T.Small + 0.04);
    },
  };
  const hbTop: Painter = (ctx) => {
    chrome(ctx, 'Homebrew app', 'App', C.IconTeal);
    entryPanel(ctx, {painter: iconOf('3dsFM')}, '3dsFM', 'ankushKun', [['Size', bytes(1_701_888)]]);
  };
  tl.tapRect(F.tapStart, hbSpec.controls[1].rect);
  tl.at(F.tap3dsx + TAP, (t) => ({
    top: hbTop,
    bottom: (ctx) => controlsBottom(ctx, hbSpec, {focus: focusAt<FocusId>([[0, 1]], t), pressed: Timeline.pressed(t, F.tapStart) ? 1 : null, t}),
  }));
  tl.cue(F.tapStart + TAP, 'select');
  // 3dsDL closes and the loader takes over: the screens go dark.
  tl.overlay((top, bottom, t) => {
    if (t < F.tapStart + TAP) return;
    const a = easeInOut(span(t, F.tapStart + TAP + 0.05, F.dark));
    for (const [ctx, w] of [
      [top, TOP_W],
      [bottom, BOTTOM_W],
    ] as [Ctx, number][]) {
      ctx.fillStyle = `rgba(0,0,0,${a})`;
      ctx.fillRect(0, 0, w, 240);
    }
  });
  return tl;
};

export {button, drawCentered, tab, listMotion};
export type {ListSpec};
