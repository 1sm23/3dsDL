import {DEMO, TITLE_SH23DS, bytes, DESTINATION, rate, duration as fmtDuration} from './data';
import {fillRound} from './draw';
import {Timeline, TAP} from './engine';
import {
  centreOf,
  dialogBottom,
  DialogSpec,
  dialogTop,
  focusAt,
  FocusId,
  progressBottom,
  ProgressView,
  progressTop,
  tabRect,
} from './kit';
import {
  ciaTop,
  confirm,
  footer,
  home,
  HOME,
  homeSpec,
  installPromptSpec,
  iconOf,
  scanner,
  Shot,
} from './screens';
import {controlsBottom} from './kit';
import {C, Ctx, easeInOut, easeOut, span} from './theme';

const at = (h: number, m: number, s = 0) => h * 3600 + m * 60 + s;

// --- Boot -------------------------------------------------------------------------------------

export const boot = () => {
  const tl = new Timeline(3.0, at(20, 7, 2));
  tl.at(0, (t) => home({focus: focusAt<FocusId>([[0, HOME.scan]], t), t}));
  tl.cue(0.4, 'boot');
  tl.overlay((top, bottom, t) => {
    for (const [ctx, w] of [
      [top, 400],
      [bottom, 320],
    ] as [Ctx, number][]) {
      if (t < 1.0) {
        ctx.fillStyle = '#000000';
        ctx.fillRect(0, 0, w, 240);
        if (t >= 0.4) {
          // The backlight comes up: a soft glow from the middle.
          const e = easeInOut(span(t, 0.4, 1.0));
          const g = ctx.createRadialGradient(w / 2, 120, 10, w / 2, 120, 60 + 260 * e);
          g.addColorStop(0, `rgba(255,253,244,${0.95 * e})`);
          g.addColorStop(1, `rgba(255,250,232,${0.75 * e * e})`);
          ctx.fillStyle = g;
          ctx.fillRect(0, 0, w, 240);
        }
      } else if (t < 1.4) {
        const e = easeOut(span(t, 1.0, 1.4));
        ctx.fillStyle = `rgba(255,252,240,${0.92 * (1 - e)})`;
        ctx.fillRect(0, 0, w, 240);
      }
    }
  });
  return tl;
};

// --- Home, idle -----------------------------------------------------------------------------------

export const homeIdle = () => {
  const tl = new Timeline(8.0, at(20, 7, 55));
  tl.at(0, (t) => home({focus: focusAt<FocusId>([[0, HOME.scan]], t), t}));
  return tl;
};

// --- Downloading ------------------------------------------------------------------------------------

// The speed shown is the app's smoothed meter: about 580 KiB/s with small
// wobbles, updated a few times a second.
const speedAt = (t: number) => {
  const q = Math.floor(t * 4) / 4;
  return 1024 * (580 + 14 * Math.sin(q * 2.1) + 9 * Math.sin(q * 5.3 + 1) + 5 * Math.sin(q * 11.7));
};

export const downloadingView = (fraction: number, t: number, measuring: boolean, tag: boolean): ProgressView => {
  const done = Math.round(DEMO.bytes * fraction);
  const speed = speedAt(t);
  return {
    title: 'Downloading',
    icon: 'Download',
    iconColor: C.IconGreen,
    name: DEMO.name,
    source: `from ${DEMO.host}`,
    fraction,
    amounts: `${bytes(done)} of ${bytes(DEMO.bytes)}`,
    speed: measuring ? 'Measuring speed...' : `${rate(speed)}, ${fmtDuration((DEMO.bytes - done) / speed)} left`,
    tag: tag ? '4 connections' : undefined,
    message: `Saving to ${DESTINATION}`,
    detail: footer,
  };
};

export const connectingView = (): ProgressView => ({
  title: 'Connecting',
  icon: 'Download',
  iconColor: C.IconGreen,
  name: DEMO.host,
  source: `from ${DEMO.host}`,
  fraction: -1,
  amounts: '0 B',
  speed: '',
  message: `Saving to ${DESTINATION}`,
  detail: footer,
});

export const progressShot = (v: ProgressView, t: number, cancelPressed = false): Shot => ({
  top: (ctx) => progressTop(ctx, v, t),
  bottom: (ctx) => progressBottom(ctx, v, t, cancelPressed),
});

// --- Scan, confirm, download ---------------------------------------------------------------------------

export const SCAN = {
  tapScan: 1.3,
  scanner: 1.45,
  found: 4.2,
  confirm: 4.6,
  tapDownload: 6.3,
  connecting: 6.45,
  downloading: 6.6,
  end: 14.0,
  endFraction: 0.71,
};

// Fast at first, then steady: a curve through 0 at the start and the end
// fraction at the end.
export const downloadCurve = (t: number, from: number, to: number, f0: number, f1: number) => {
  const x = span(t, from, to);
  const shaped = 0.55 * (1 - Math.pow(1 - x, 2.2)) + 0.45 * x;
  return f0 + (f1 - f0) * shaped;
};

export const qrScan = () => {
  const tl = new Timeline(14.0, at(20, 7, 40));
  const homeFocus: [number, FocusId][] = [
    [0, HOME.enterUrl],
    [0.75, HOME.scan],
  ];
  tl.cue(0.75, 'move');
  const scanTile = homeSpec().controls[HOME.scan].rect;
  tl.tapRect(SCAN.tapScan, scanTile);
  tl.at(0, (t) =>
    home({focus: focusAt(homeFocus, t), pressed: Timeline.pressed(t, SCAN.tapScan) ? HOME.scan : null, t}),
  );
  tl.at(
    SCAN.scanner,
    (t) => {
      const settle = easeInOut(span(t, 1.7, 4.05));
      return scanner(t, settle, t >= SCAN.found ? 'QR code found' : 'Looking for a QR code...');
    },
    0.08,
  );
  tl.cue(SCAN.found, 'chime');
  const downloadTab = tabRect('right');
  tl.tap(SCAN.tapDownload, downloadTab.x + downloadTab.w / 2 + 6, downloadTab.y + downloadTab.h / 2 + 2);
  tl.at(SCAN.confirm, (t) =>
    confirm(
      {focus: focusAt<FocusId>([[0, 'action']], t), pressed: Timeline.pressed(t, SCAN.tapDownload) ? 'action' : null, t},
      DEMO.name,
      DEMO.url,
      DEMO.host,
    ),
  );
  tl.cue(SCAN.tapDownload + TAP, 'select');
  tl.at(SCAN.connecting, (t) => progressShot(connectingView(), t));
  tl.at(
    SCAN.downloading,
    (t) => {
      // At the meter's rate: a 24 MiB file is a few percent in by the end of
      // the shot, with about 40 seconds left.
      const f = Math.min(1, (Math.max(0, t - SCAN.downloading - 0.15) * 580 * 1024) / DEMO.bytes);
      return progressShot(downloadingView(f, t, t < SCAN.downloading + 0.45, t >= SCAN.downloading + 0.3), t);
    },
    0.05,
  );
  return tl;
};

// --- Install ----------------------------------------------------------------------------------------------

export const INSTALLED_DIALOG: DialogSpec = {
  header: 'Installation Complete',
  title: 'Silent Hill 2 has been installed. You can find it on the HOME Menu.',
  tone: 'success',
  icon: iconOf('sh23ds'),
  body: [
    {text: 'Silent Hill 2', maxLines: 1},
    {text: `Game or app, ${TITLE_SH23DS.version}, ${bytes(TITLE_SH23DS.bytes)}, installed in 10s`, color: C.InkSoft, maxLines: 2},
    {text: 'The CIA file was deleted.', color: C.InkSoft, maxLines: 2},
  ],
  buttons: ['Close', 'Launch'],
};

export const installingView = (fraction: number, t: number): ProgressView => {
  const done = Math.round(TITLE_SH23DS.bytes * fraction);
  const speed = 1024 * 1024 * (2.36 + 0.05 * Math.sin(Math.floor(t * 4) * 1.7));
  return {
    title: 'Installing',
    icon: 'Package',
    iconColor: C.IconBlue,
    name: TITLE_SH23DS.name,
    source: DEMO.name,
    titleIcon: iconOf('sh23ds'),
    fraction,
    amounts: `${bytes(done)} of ${bytes(TITLE_SH23DS.bytes)}`,
    speed: fraction < 0.08 ? '' : `${rate(speed)}, ${fmtDuration((TITLE_SH23DS.bytes - done) / speed)} left`,
    message: 'Do not turn off the power or remove the SD card.',
    detail: `Installing from ${DESTINATION}${DEMO.name}`,
  };
};

export const INSTALL = {
  done: 1.0,
  prompt: 1.15,
  move: 1.6,
  tapBoth: 2.0,
  installing: 2.2,
  installed: 4.0,
  dialog: 4.15,
  tapLaunch: 5.4,
};

export const install = () => {
  const tl = new Timeline(6.0, at(20, 8, 12));
  tl.at(0, (t) => {
    const f = Math.min(1, SCAN.endFraction + (1 - SCAN.endFraction) * easeInOut(span(t, 0, INSTALL.done)));
    return progressShot(downloadingView(f, t + 20, false, true), t);
  });
  tl.cue(INSTALL.done, 'done');
  const promptSpec = installPromptSpec('Not installed yet.');
  const promptTop = ciaTop(TITLE_SH23DS, 'Download Complete', 'Download', C.IconGreen, '0.2.2, not installed', `${bytes(TITLE_SH23DS.bytes)}, SD card`);
  const focus: [number, FocusId][] = [
    [0, 0],
    [INSTALL.move, 1],
  ];
  tl.cue(INSTALL.move, 'move');
  tl.tapRect(INSTALL.tapBoth, promptSpec.controls[1].rect);
  tl.at(INSTALL.prompt, (t) => ({
    top: promptTop,
    bottom: (ctx) => controlsBottom(ctx, promptSpec, {focus: focusAt(focus, t), pressed: Timeline.pressed(t, INSTALL.tapBoth) ? 1 : null, t}),
  }));
  tl.cue(INSTALL.tapBoth + TAP, 'select');
  const installingAt = (t: number) => installingView(easeInOut(span(t, INSTALL.installing + 0.1, INSTALL.installed)), t);
  tl.at(INSTALL.installing, (t) => progressShot(installingAt(t), t));
  const launch = INSTALLED_DIALOG.buttons!.indexOf('Launch');
  const launchRect = {x: 160, y: 189, w: 150, h: 39};
  tl.tap(INSTALL.tapLaunch, launchRect.x + launchRect.w / 2, launchRect.y + launchRect.h / 2, 6.0 - INSTALL.tapLaunch);
  const last = installingAt(INSTALL.installed + 0.2);
  tl.at(
    INSTALL.dialog,
    (t, local) => ({
      top: (ctx) => dialogTop(ctx, INSTALLED_DIALOG),
      bottom: (ctx) =>
        dialogBottom(ctx, INSTALLED_DIALOG, {selected: launch, pressed: t >= INSTALL.tapLaunch ? launch : -1, age: local, t}, (c) =>
          progressBottom(c, last, INSTALL.installed),
        ),
    }),
    0,
  );
  tl.cue(INSTALL.dialog, 'installed');
  return tl;
};

export {centreOf, fillRound, controlsBottom};
