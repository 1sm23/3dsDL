import {bytes, plural, SAVE_BACKUP_NAME, SAVE_BACKUPS_BEFORE, SAVE_FILES, SAVE_FOLDER, SAVE_GAME, SOFTWARE} from './data';
import {drawText, panel, tagRow, titleIcon, TOP_PANEL} from './draw';
import {Timeline, TAP} from './engine';
import {
  busyBottom,
  busyTop,
  controlsBottom,
  dialogBottom,
  DialogSpec,
  dialogTop,
  focusAt,
  FocusId,
  KEYBOARD_OK,
  keyboardBottom,
  keyboardTop,
  listBottom,
  listMotion,
  ListSpec,
  Painter,
  rowRect,
  tabRect,
  titleIconFramed,
} from './kit';
import {chrome, iconOf, Shot, titlesSpec, titlesTop, titleTop} from './screens';
import {drawMarquee, drawWrapped} from './text';
import {C, Ctx, T} from './theme';

const at = (h: number, m: number, s = 0) => h * 3600 + m * 60 + s;

const COUNTS = {
  software: SOFTWARE.length,
  sdBytes: SOFTWARE.reduce((n, t) => n + t.size, 0),
  tickets: SOFTWARE.length + 2,
  addOns: 1,
  extData: 4,
};

// Tickets sorted by name: a Title ID first (software that is gone), then
// the installed software, with the Starfall Racers update's own ticket.
const TICKETS = [
  {name: '000400000F8C2100', kind: '', unused: true, icon: null as string | null},
  ...SOFTWARE.flatMap((t) =>
    t.name === 'Starfall Racers'
      ? [
          {name: t.name, kind: 'Game or app', unused: false, icon: t.icon},
          {name: t.name, kind: 'Update', unused: false, icon: t.icon},
        ]
      : [{name: t.name, kind: 'Game or app', unused: false, icon: t.icon}],
  ),
];

const ticketsSpec = (): ListSpec => ({
  title: 'Tickets',
  items: TICKETS.map((k) =>
    k.unused ? {label: k.name, icon: 'Lock', iconColor: C.IconGrey, badge: 'Unused', badgeColor: C.Neutral} : {label: k.name, icon: 'Lock', iconColor: C.IconGrey, detail: k.kind},
  ),
  action: 'Delete Unused',
  actionKey: 'X',
});

const ticketTop =
  (index: number): Painter =>
  (ctx) => {
    const k = TICKETS[index];
    chrome(ctx, 'Tickets', 'Lock', C.IconGrey);
    panel(ctx);
    const left = TOP_PANEL.x + 18;
    const top = TOP_PANEL.y + 12;
    const title = SOFTWARE.find((s) => s.name === k.name);
    if (k.icon) titleIconFramed(ctx, left, top, iconOf(k.icon));
    else titleIconFallback(ctx, left, top);
    drawMarquee(ctx, k.name, left + 60, top + 2, 0.62, C.InkStrong, 280);
    drawText(ctx, k.unused ? 'Game or app' : k.kind, left + 60, top + 30, T.Small, C.InkSoft);
    const id = k.unused ? k.name : k.kind === 'Update' ? title!.titleId.replace(/^00040000/, '0004000E') : title!.titleId;
    let y = tagRow(ctx, left, top + 60, 96, 'Title ID', id);
    y = tagRow(ctx, left, y, 96, 'Software', k.unused ? 'Not installed' : 'Installed', k.unused ? C.Neutral : C.Tag);
    drawWrapped(
      ctx,
      k.unused ? 'Safe to delete. Installing a CIA again brings its ticket back.' : 'The software will not start without its ticket.',
      left,
      y + 2,
      T.Tiny,
      k.unused ? C.InkSoft : C.Caution,
      TOP_PANEL.w - 36,
      2,
    );
  };

// drawTitleIcon's stand-in when there is no icon: dark, with a light glyph.
const titleIconFallback = (ctx: Ctx, x: number, y: number) => titleIcon(ctx, x, y, null, 'Lock');

const softwareSpec = (): ListSpec => ({
  title: 'Software',
  items: SOFTWARE.map((t) => ({label: t.name, icon: 'GameCard', iconColor: C.IconBlue, detail: bytes(t.size)})),
});

// Save Data: software with a save, and how many backups each has.
const SAVES = [
  {title: SOFTWARE.find((t) => t.name === 'Pocket Orchard')!, folder: 'sdmc:/3ds/Checkpoint/saves/0x0C4A3 Pocket Orchard/', backups: 2},
  {title: SAVE_GAME, folder: SAVE_FOLDER, backups: SAVE_BACKUPS_BEFORE.length},
];

const savesSpec = (): ListSpec => ({
  title: 'Save Data',
  items: SAVES.map((s) => ({label: s.title.name, icon: 'Chip', iconColor: C.IconTeal, detail: plural(s.backups, 'backup')})),
});

const savesTop =
  (index: number): Painter =>
  (ctx) => {
    const s = SAVES[index];
    chrome(ctx, 'Save Data', 'Chip', C.IconTeal);
    panel(ctx);
    const left = TOP_PANEL.x + 18;
    const top = TOP_PANEL.y + 12;
    titleIconFramed(ctx, left, top, iconOf(s.title.icon));
    drawMarquee(ctx, s.title.name, left + 60, top + 2, 0.62, C.InkStrong, 280);
    drawMarquee(ctx, s.title.publisher, left + 60, top + 30, T.Small, C.InkSoft, 280);
    const y = tagRow(ctx, left, top + 60, 96, 'Title ID', s.title.titleId);
    tagRow(ctx, left, y, 96, 'Backups in', s.folder);
  };

const backupsSpec = (names: string[]): ListSpec => ({
  title: SAVE_GAME.name,
  items: [
    {label: 'New Backup', icon: 'Download', iconColor: C.IconGreen, labelColor: C.Accent},
    ...names.map((n) => ({label: n, icon: 'History' as const, iconColor: C.IconTeal})),
  ],
  action: 'Browse',
  actionKey: 'X',
});

const backupsTop =
  (names: string[], highlighted: number): Painter =>
  (ctx) => {
    chrome(ctx, 'Save Data', 'Chip', C.IconTeal);
    panel(ctx);
    const left = TOP_PANEL.x + 18;
    const top = TOP_PANEL.y + 12;
    titleIconFramed(ctx, left, top, iconOf(SAVE_GAME.icon));
    drawMarquee(ctx, SAVE_GAME.name, left + 60, top + 2, 0.62, C.InkStrong, 280);
    drawText(ctx, plural(names.length, 'backup'), left + 60, top + 30, T.Small, C.InkSoft);
    const y = tagRow(ctx, left, top + 60, 76, 'Folder', SAVE_FOLDER);
    drawWrapped(
      ctx,
      highlighted <= 0
        ? "Copies the save data into a new backup. Checkpoint uses the same folders, so each app sees the other's backups."
        : 'Restore puts this backup back, replacing the save data on the console.',
      left,
      y + 2,
      T.Tiny,
      C.InkSoft,
      TOP_PANEL.w - 36,
      3,
    );
  };

export const TITLES = {
  tapSoftware: 1.5,
  software: 1.65,
  tapBack1: 4.7,
  menu2: 4.85,
  tapTickets: 5.05,
  tickets: 5.2,
  tapBack2: 7.0,
  menu3: 7.15,
  tapSaves: 7.35,
  busy: 7.5,
  saves: 7.75,
  tapGame: 8.0,
  backups: 8.15,
  tapNew: 8.6,
  keyboard: 8.75,
  tapOk: 9.0,
  working: 9.15,
  done: 10.2,
};

export const BACKED_UP: DialogSpec = {
  header: 'Backed Up',
  title: `${plural(SAVE_FILES, 'file')} saved as ${SAVE_BACKUP_NAME}.`,
  tone: 'success',
  body: [{text: SAVE_FOLDER + SAVE_BACKUP_NAME, color: C.InkSoft, maxLines: 5}],
};

export const titles = () => {
  const X = TITLES;
  const tl = new Timeline(12.0, at(20, 15, 30));
  const spec = titlesSpec();
  const menuFocus: [number, FocusId][] = [
    [0, 3],
    [0.6, 4],
    [1.0, 3],
    [X.tapSoftware, 3],
    [X.tapTickets, 1],
    [X.tapSaves, 5],
  ];
  tl.cue(0.6, 'move').cue(1.0, 'move');
  const menuShot = (t: number, pressedAt: number, pressedId: number): Shot => {
    const f = focusAt(menuFocus, t);
    return {
      top: titlesTop(typeof f.id === 'number' ? f.id : 3, COUNTS),
      bottom: (ctx) => controlsBottom(ctx, spec, {focus: f, pressed: Timeline.pressed(t, pressedAt) ? pressedId : null, t}),
    };
  };
  tl.tapRect(X.tapSoftware, spec.controls[3].rect);
  tl.at(0, (t) => menuShot(t, X.tapSoftware, 3));
  tl.cue(X.tapSoftware + TAP, 'select');

  // T1: Software.
  const sSpec = softwareSpec();
  const sKeys: [number, number][] = [
    [X.software, 0],
    [2.6, 1],
    [3.2, 2],
  ];
  tl.cue(2.6, 'move').cue(3.2, 'move');
  const back = tabRect('left');
  tl.tapRect(X.tapBack1, back);
  tl.at(X.software, (t) => {
    const m = listMotion(sSpec, sKeys, t);
    return {
      top: titleTop('Software', 'GameCard', SOFTWARE[m.cursor]),
      bottom: (ctx) => listBottom(ctx, sSpec, {...m, t, pressed: Timeline.pressed(t, X.tapBack1) ? 'back' : null}),
    };
  });
  tl.cue(X.tapBack1 + TAP, 'back');

  // T2: Tickets.
  tl.tapRect(X.tapTickets, spec.controls[1].rect);
  tl.at(X.menu2, (t) => menuShot(t, X.tapTickets, 1));
  tl.cue(X.tapTickets + TAP, 'select');
  const kSpec = ticketsSpec();
  const kKeys: [number, number][] = [
    [X.tickets, 0],
    [5.9, 1],
    [6.4, 2],
  ];
  tl.cue(5.9, 'move').cue(6.4, 'move');
  tl.tapRect(X.tapBack2, back);
  tl.at(X.tickets, (t) => {
    const m = listMotion(kSpec, kKeys, t);
    return {top: ticketTop(m.cursor), bottom: (ctx) => listBottom(ctx, kSpec, {...m, t, pressed: Timeline.pressed(t, X.tapBack2) ? 'back' : null})};
  });
  tl.cue(X.tapBack2 + TAP, 'back');

  // T3: Save Data, a new backup in Checkpoint's folder.
  tl.tapRect(X.tapSaves, spec.controls[5].rect);
  tl.at(X.menu3, (t) => menuShot(t, X.tapSaves, 5));
  tl.cue(X.tapSaves + TAP, 'select');
  const menuUnder = menuShot(X.busy, -1, -1);
  tl.at(
    X.busy,
    (t, local) => ({
      top: (ctx) => busyTop(ctx, 'Save Data', 'Looking for save data...', menuUnder.top),
      bottom: (ctx) => busyBottom(ctx, menuUnder.bottom, local, t),
    }),
    0,
  );
  const vSpec = savesSpec();
  const vKeys: [number, number][] = [[X.saves, 0]];
  const gameRow = rowRect(vSpec, 1, 0);
  tl.tapRect(X.tapGame, gameRow);
  tl.at(X.saves, (t) => {
    const m = listMotion(vSpec, vKeys, t);
    const held = Timeline.pressed(t, X.tapGame) ? 1 : undefined;
    return {top: savesTop(m.cursor), bottom: (ctx) => listBottom(ctx, vSpec, {...m, t, held, ring: held == null})};
  });
  tl.cue(X.tapGame + TAP, 'select');
  const bSpec = backupsSpec(SAVE_BACKUPS_BEFORE);
  const newRow = rowRect(bSpec, 0, 0);
  tl.tapRect(X.tapNew, newRow);
  const backupsShot = (t: number): Shot => {
    const held = Timeline.pressed(t, X.tapNew) ? 0 : undefined;
    return {top: backupsTop(SAVE_BACKUPS_BEFORE, 0), bottom: (ctx) => listBottom(ctx, bSpec, {cursor: 0, t, held, ring: held == null})};
  };
  tl.at(X.backups, (t) => backupsShot(t));
  tl.cue(X.tapNew + TAP, 'select');
  const under = backupsShot(X.keyboard);
  tl.tapRect(X.tapOk, KEYBOARD_OK);
  tl.at(
    X.keyboard,
    (t, local) => ({
      top: (ctx) => keyboardTop(ctx, 'Backup name (up to 20 characters)', SAVE_BACKUP_NAME, under.top, local, t),
      bottom: (ctx) => {
        under.bottom(ctx);
        keyboardBottom(ctx, null, Timeline.pressed(t, X.tapOk), local, SAVE_BACKUP_NAME);
      },
    }),
    0,
  );
  tl.cue(X.tapOk + TAP, 'select');
  tl.at(
    X.working,
    (t, local) => ({
      top: (ctx) => busyTop(ctx, 'Backup', 'Backing up the save data...', under.top),
      bottom: (ctx) => busyBottom(ctx, under.bottom, local, t),
    }),
    0,
  );
  tl.at(
    X.done,
    (t, local) => ({
      top: (ctx) => dialogTop(ctx, BACKED_UP),
      bottom: (ctx) => dialogBottom(ctx, BACKED_UP, {selected: 0, age: local + 0.2, t}, under.bottom),
    }),
    0.06,
  );
  tl.cue(X.done, 'done');
  return tl;
};
