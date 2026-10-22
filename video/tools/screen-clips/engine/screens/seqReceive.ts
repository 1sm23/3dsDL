import {DOWNLOADED_FILE, RECEIVE, RECEIVE_ADDRESS, RECEIVE_IP, REMOTE_INSTALL, SENT_BYTES, SENT_NAME, UPLOADS} from '../scripts/receive';
import {bytes, DESTINATION, rate, duration as fmtDuration, TITLE_ANEMONE} from './data';
import {Timeline, TAP} from './engine';
import {dialogBottom, dialogButtonRect, DialogSpec, dialogTop, Painter, progressBottom, ProgressView, tabRect} from './kit';
import {footer, iconOf, receiveBottom, receiveTop, Shot, Transfer} from './screens';
import {progressShot} from './seqMain';
import {C, easeInOut, span} from './theme';

const at = (h: number, m: number, s = 0) => h * 3600 + m * 60 + s;

// The computer the page is open on.
export const LAPTOP_IP = '192.168.1.50';

// What the console does in the Receive scene, beyond src/scripts/receive.ts.
// The computer is already allowed to use the SD card (see receive()).
export const RECEIVE_CONSOLE = {
  // Contact; Download is chosen as the stylus lifts, at 6.8.
  downloadTapped: 6.65,
  // The file is in by 8.6; the queue asks once about CIAs (Keep Files) and
  // says Queue Done; OK lets the page hear the result at about 9.45.
  downloaded: 8.6,
  keepFilesTapped: 8.85,
  queueDoneOkTapped: 9.18,
  // Install is chosen as the stylus lifts, at 14.75.
  installTapped: 14.6,
  installedOkTapped: RECEIVE.installDone + 0.3,
};

const page = RECEIVE_IP;
const named = RECEIVE_ADDRESS;
const ip = RECEIVE_IP.replace(/^http:\/\//, '').replace(/\/$/, '');

const ALLOW: DialogSpec = {
  header: 'Receive',
  title: `Let ${LAPTOP_IP} use the SD card?`,
  tone: 'warning',
  body: [
    {text: `A device at ${LAPTOP_IP} wants to open the SD card's files from the 3dsDL page.`, maxLines: 3},
    {text: 'It could add, change and delete any file while Receive is open. Only allow your own phone or computer.', color: C.InkSoft, maxLines: 3},
  ],
  buttons: ['Allow', "Don't Allow"],
};

const LINK: DialogSpec = {
  header: 'Receive',
  title: `Received 1 link from ${LAPTOP_IP}.`,
  tone: 'info',
  body: [
    {text: SENT_NAME, maxLines: 3},
    {text: `They go to ${DESTINATION}.`, color: C.InkSoft, maxLines: 2},
  ],
  buttons: ['Download', 'Add to Queue', 'Cancel'],
};

const ASK_INSTALL_QUEUE: DialogSpec = {
  header: 'Queue',
  title: 'Install the CIAs from this queue?',
  tone: 'info',
  body: [{text: `${SENT_NAME} is the first. The answer is used for every CIA in this queue.`, maxLines: 3}],
  buttons: ['Install', 'Install, Delete', 'Keep Files'],
};

const QUEUE_DONE: DialogSpec = {
  header: 'Queue Done',
  title: '1 file downloaded.',
  tone: 'success',
  body: [{text: `Saved: ${SENT_NAME}.`, color: C.InkSoft, maxLines: 5}],
};

const INSTALL_SENT: DialogSpec = {
  header: 'Receive',
  title: `Install 1 CIA sent from ${LAPTOP_IP}?`,
  tone: 'info',
  body: [{text: REMOTE_INSTALL, maxLines: 3}],
  buttons: ['Install', 'Install and Delete', 'Not Now'],
};

const INSTALLED: DialogSpec = {
  header: 'Installed',
  title: `Installed ${REMOTE_INSTALL}.`,
  tone: 'success',
  body: [{text: 'Find it on the HOME Menu.', color: C.InkSoft, maxLines: 5}],
};

// Uploads one after another, each taking time in proportion to its size.
// Uploads one after another, as the page sends them.
const UPLOAD_ENDS = [12.15, 12.3, 13.25];
const UPLOAD_SPANS = UPLOADS.map((u, i) => ({name: u.name, bytes: u.bytes, from: i === 0 ? 11.15 : UPLOAD_ENDS[i - 1], to: UPLOAD_ENDS[i]}));

const uploadAt = (t: number): Transfer | null => {
  for (const u of UPLOAD_SPANS) {
    if (t >= u.from && t < u.to) {
      const f = span(t, u.from, u.to);
      const r = u.bytes / (u.to - u.from);
      return {upload: true, name: u.name, done: Math.round(u.bytes * f), total: u.bytes, rate: f > 0.15 ? r * (1 + 0.03 * Math.sin(t * 9)) : 0, from: LAPTOP_IP};
    }
  }
  return null;
};

const sendAt = (t: number): Transfer | null => {
  if (t < RECEIVE.downloadClicked || t >= RECEIVE.downloadDone) return null;
  const f = span(t, RECEIVE.downloadClicked, RECEIVE.downloadDone);
  const r = DOWNLOADED_FILE.bytes / (RECEIVE.downloadDone - RECEIVE.downloadClicked);
  return {upload: false, name: DOWNLOADED_FILE.name, done: Math.round(DOWNLOADED_FILE.bytes * f), total: DOWNLOADED_FILE.bytes, rate: f > 0.15 ? r : 0, from: LAPTOP_IP};
};

const lastAt = (t: number) => {
  const uploaded = UPLOAD_SPANS.filter((u) => t >= u.to);
  if (uploaded.length) return `${uploaded[uploaded.length - 1].name} from ${LAPTOP_IP}`;
  if (t >= RECEIVE_CONSOLE.queueDoneOkTapped + TAP) return `1 link from ${LAPTOP_IP}`;
  return '';
};

const receiveShot = (t: number, backPressed = false): Shot => ({
  top: receiveTop(page, ip, t),
  bottom: receiveBottom(page, named, lastAt(t), uploadAt(t) ?? sendAt(t), t, backPressed),
});

const dialogShot = (d: DialogSpec, under: Painter, selected: number, tapAt: number, index: number, t: number, local: number): Shot => ({
  top: (ctx) => dialogTop(ctx, d),
  bottom: (ctx) => dialogBottom(ctx, d, {selected, pressed: Timeline.pressed(t, tapAt) ? index : -1, age: local, t}, under),
});

const downloadView = (t: number, from: number, to: number): ProgressView => {
  const f = easeInOut(span(t, from, to));
  const done = Math.round(SENT_BYTES * f);
  const speed = (SENT_BYTES / (to - from)) * (1 + 0.025 * Math.sin(Math.floor(t * 4) * 2.3));
  return {
    title: 'Downloading 1 of 1',
    icon: 'Download',
    iconColor: C.IconGreen,
    name: SENT_NAME,
    source: 'from github.com',
    fraction: f,
    amounts: `${bytes(done)} of ${bytes(SENT_BYTES)}`,
    speed: t < from + 0.4 ? 'Measuring speed...' : `${rate(speed)}, ${fmtDuration((SENT_BYTES - done) / speed)} left`,
    tag: t >= from + 0.25 ? '4 connections' : undefined,
    message: `Saving to ${DESTINATION}`,
    detail: footer,
  };
};

const installView = (t: number, from: number, to: number): ProgressView => {
  const f = easeInOut(span(t, from, to));
  const total = UPLOADS.find((u) => u.name === REMOTE_INSTALL)!.bytes;
  const done = Math.round(total * f);
  const speed = (total / (to - from)) * 1.1;
  return {
    title: 'Installing',
    icon: 'Package',
    iconColor: C.IconBlue,
    name: TITLE_ANEMONE.name,
    source: REMOTE_INSTALL,
    titleIcon: iconOf(TITLE_ANEMONE.icon),
    fraction: f,
    amounts: `${bytes(done)} of ${bytes(total)}`,
    speed: f < 0.1 ? '' : `${rate(speed)}, ${fmtDuration((total - done) / speed)} left`,
    message: 'Do not turn off the power or remove the SD card.',
    detail: `Installing from ${DESTINATION}${REMOTE_INSTALL}`,
  };
};

export const receive = () => {
  const K = RECEIVE_CONSOLE;
  const tl = new Timeline(RECEIVE.duration, at(20, 16, 40));
  tl.at(0, (t) => receiveShot(t));

  // The computer was allowed to use the SD card on an earlier visit, as the
  // browser page assumes, so no Allow prompt appears in this scene. ALLOW
  // stays defined for a cut that wants to show it.

  // A link arrives; Download; it downloads.
  const linkUnder = receiveShot(RECEIVE.linkReceived).bottom;
  tl.tapRect(K.downloadTapped, dialogButtonRect(LINK, 0));
  tl.at(RECEIVE.linkReceived, (t, local) => dialogShot(LINK, linkUnder, 0, K.downloadTapped, 0, t, local), 0);
  tl.cue(RECEIVE.linkReceived, 'notify');
  tl.cue(K.downloadTapped + TAP, 'select');
  const connecting = K.downloadTapped + TAP;
  const dataFrom = connecting + 0.1;
  tl.at(connecting, (t) =>
    progressShot({...downloadView(t, dataFrom, K.downloaded), title: 'Connecting 1 of 1', name: 'github.com', fraction: -1, amounts: '0 B', speed: '', tag: undefined}, t),
  );
  tl.at(dataFrom, (t) => progressShot(downloadView(t, dataFrom, K.downloaded), t), 0.04);
  tl.cue(K.downloaded, 'done');

  // A CIA landed: the queue asks once; Keep Files, then Queue Done.
  const lastDl = downloadView(K.downloaded, dataFrom, K.downloaded);
  const dlUnder: Painter = (c) => progressBottom(c, lastDl, K.downloaded);
  const askAt = K.downloaded + 0.05;
  tl.tapRect(K.keepFilesTapped, dialogButtonRect(ASK_INSTALL_QUEUE, 2));
  tl.at(askAt, (t, local) => dialogShot(ASK_INSTALL_QUEUE, dlUnder, 0, K.keepFilesTapped, 2, t, local), 0);
  tl.cue(K.keepFilesTapped + TAP, 'select');
  tl.tapRect(K.queueDoneOkTapped, dialogButtonRect(QUEUE_DONE, 0));
  tl.at(K.keepFilesTapped + TAP + 0.03, (t, local) => dialogShot(QUEUE_DONE, dlUnder, 0, K.queueDoneOkTapped, 0, t, local + 0.2), 0.06);
  tl.cue(K.queueDoneOkTapped + TAP, 'select');
  tl.at(K.queueDoneOkTapped + TAP, (t) => receiveShot(t), 0.06);
  tl.cue(UPLOAD_ENDS[UPLOAD_ENDS.length - 1], 'done');

  // The page asks to install the uploaded CIA.
  const installUnder = receiveShot(RECEIVE.installClicked).bottom;
  tl.tapRect(K.installTapped, dialogButtonRect(INSTALL_SENT, 0));
  tl.at(RECEIVE.installClicked, (t, local) => dialogShot(INSTALL_SENT, installUnder, 0, K.installTapped, 0, t, local), 0);
  tl.cue(RECEIVE.installClicked, 'notify');
  tl.cue(K.installTapped + TAP, 'select');
  const iFrom = K.installTapped + TAP;
  const iTo = RECEIVE.installDone - 0.15;
  tl.at(iFrom, (t) => progressShot(installView(t, iFrom + 0.08, iTo), t));
  const lastI = installView(iTo, iFrom + 0.08, iTo);
  tl.tapRect(K.installedOkTapped, dialogButtonRect(INSTALLED, 0));
  tl.at(RECEIVE.installDone, (t, local) => dialogShot(INSTALLED, (c) => progressBottom(c, lastI, t), 0, K.installedOkTapped, 0, t, local), 0);
  tl.cue(RECEIVE.installDone, 'installed');
  tl.cue(K.installedOkTapped + TAP, 'select');
  tl.at(K.installedOkTapped + TAP, (t) => receiveShot(t), 0.06);
  return tl;
};

export {tabRect};
