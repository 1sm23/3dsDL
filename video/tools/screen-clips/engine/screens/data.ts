import {DEMO_CIA_BYTES, DEMO_CIA_URL} from '../lib/links';

// What is on the console in the video: plausible homebrew, original games.

export const FREE_SPACE = '26.8 GiB free';
export const DESTINATION = 'sdmc:/cias/';

// Formats like units::bytes.
export const bytes = (count: number) => {
  if (count < 1024) return `${Math.round(count)} B`;
  let value = count / 1024;
  const suffixes = ['KiB', 'MiB', 'GiB', 'TiB'];
  let i = 0;
  while (value >= 1024 && i + 1 < suffixes.length) {
    value /= 1024;
    i++;
  }
  // %.2f / %.1f round half to even only on exact ties; this is close enough.
  return `${value < 10 ? value.toFixed(2) : value.toFixed(1)} ${suffixes[i]}`;
};
export const rate = (perSecond: number) => `${bytes(Math.max(0, Math.floor(perSecond)))}/s`;
export const duration = (seconds: number) => {
  const s = Math.max(0, Math.round(seconds));
  if (s < 60) return `${s}s`;
  if (s < 3600) return `${Math.floor(s / 60)}m ${String(s % 60).padStart(2, '0')}s`;
  return `${Math.floor(s / 3600)}h ${String(Math.floor(s / 60) % 60).padStart(2, '0')}m`;
};
export const plural = (n: number, word: string) => `${n} ${word}${n === 1 ? '' : 's'}`;

export type Title = {
  name: string;
  publisher: string;
  titleId: string;
  version: string; // "0.7.2 (v114)"
  bytes: number;
  icon: string; // key in pictures.ICONS
};

export const DEMO = {
  url: DEMO_CIA_URL,
  name: 'sh23ds.cia',
  bytes: DEMO_CIA_BYTES,
  host: 'github.com',
};

// The title the scanned CIA installs: its SMDH names it Silent Hill 2
// (unique ID 0xF5D23, version 0.2.2).
export const TITLE_SH23DS: Title = {
  name: 'Silent Hill 2',
  publisher: 'sh23ds by ankushKun',
  titleId: '000400000F5D2300',
  version: '0.2.2 (v34)',
  bytes: DEMO_CIA_BYTES,
  icon: 'sh23ds',
};

export const TITLE_3DSFM: Title = {
  name: '3dsFM',
  publisher: 'ankushKun',
  titleId: '0004000003D5F100',
  version: '0.7.2 (v114)',
  bytes: 4_510_656,
  icon: '3dsFM',
};

export const TITLE_CHECKPOINT: Title = {
  name: 'Checkpoint',
  publisher: 'FlagBrew',
  titleId: '000400000BCFFF00',
  version: '3.10.1 (v3745)',
  bytes: 2_386_368,
  icon: 'Checkpoint',
};

export const TITLE_ANEMONE: Title = {
  name: 'Anemone3DS',
  publisher: 'astronautlevel',
  titleId: '000400000BAFEE00',
  version: '3.0.2 (v3106)',
  bytes: 1_966_080,
  icon: 'Anemone3DS',
};

// Software, sorted by name as titles::sortByName does.
export const SOFTWARE: (Title & {size: number})[] = [
  {name: '3dsDL', publisher: 'ankushKun', titleId: '000400000D921E00', version: '0.8.2 (v2095)', bytes: 2_054_144, size: 2_621_440, icon: '3dsDL'},
  {...TITLE_3DSFM, size: 5_242_880},
  {...TITLE_ANEMONE, size: 2_752_512},
  {...TITLE_CHECKPOINT, size: 3_145_728},
  {name: 'ftpd', publisher: 'mtheall', titleId: '000400000BCFFE00', version: '3.2.1 (v3217)', bytes: 1_048_576, size: 1_572_864, icon: 'ftpd'},
  {name: 'Pocket Orchard', publisher: 'Little Lantern', titleId: '0004000000C4A300', version: '1.1.0 (v1040)', bytes: 0, size: 98_566_144, icon: 'Pocket Orchard'},
  {name: 'Starfall Racers', publisher: 'Night Kite Games', titleId: '00040000001B7A00', version: '1.2.0 (v1056)', bytes: 0, size: 412_090_368, icon: 'Starfall Racers'},
  {name: 'Universal-Updater', publisher: 'Universal-Team', titleId: '0004000004391700', version: '3.2.4 (v3140)', bytes: 0, size: 3_670_016, icon: 'Universal-Updater'},
];

// The save the video backs up, and where Checkpoint keeps it
// (saves::titleFolder: "0x" + the unique ID in five digits + the name).
export const SAVE_GAME = SOFTWARE.find((t) => t.name === 'Starfall Racers')!;
export const SAVE_FOLDER = 'sdmc:/3ds/Checkpoint/saves/0x01B7A Starfall Racers/';
export const SAVE_BACKUPS_BEFORE = ['20261012-183522'];
export const SAVE_BACKUP_NAME = '20261020-201431';
export const SAVE_FILES = 3;

// --- Files ------------------------------------------------------------------------------------

export type Entry = {name: string; folder?: boolean; bytes?: number; items?: number; changed: string};

export const ROOT_ENTRIES: Entry[] = [
  {name: '3ds', folder: true, items: 14, changed: '2026-10-18 21:40'},
  {name: 'cias', folder: true, items: 6, changed: '2026-10-20 19:58'},
  {name: 'luma', folder: true, items: 3, changed: '2026-09-02 10:12'},
  {name: 'Nintendo 3DS', folder: true, items: 1, changed: '2026-03-14 09:30'},
  {name: 'pictures', folder: true, items: 2, changed: '2026-10-19 22:05'},
  {name: '3dsFM.3dsx', bytes: 1_701_888, changed: '2026-10-20 19:41'},
  {name: 'Checkpoint.cia', bytes: 2_386_368, changed: '2026-10-20 19:44'},
  {name: 'notes.txt', bytes: 214, changed: '2026-10-20 18:02'},
  {name: 'themes.zip', bytes: 1_843_200, changed: '2026-10-20 19:50'},
];

// After themes.zip is extracted into sdmc:/, its Themes folder is there too.
export const ROOT_AFTER_EXTRACT: Entry[] = [
  ...ROOT_ENTRIES.slice(0, 5),
  {name: 'Themes', folder: true, items: 2, changed: '2026-10-20 20:15'},
  ...ROOT_ENTRIES.slice(5),
];

export const PICTURES: (Entry & {width: number; height: number; photo: 'sunset' | 'aurora'})[] = [
  {name: 'lake-sunset.jpg', bytes: 397_414, changed: '2026-10-19 22:04', width: 1600, height: 960, photo: 'sunset'},
  {name: 'northern-lights.jpg', bytes: 422_502, changed: '2026-10-19 22:05', width: 1600, height: 960, photo: 'aurora'},
];

export const NOTES_BEFORE = [
  '# 3DS notes',
  'Back up saves before updating Luma3DS',
  'Themes go in sdmc:/Themes/',
  'Try the new 3dsFM build',
  '',
  '[todo]',
  '- clean up the cias folder',
  '- send the screenshots to my PC',
];
export const NOTES_EDIT_LINE = 3;
export const NOTES_TYPED = ' on the train';

export const THEMES_ZIP = {
  name: 'themes.zip',
  fileCount: 6,
  totalSize: 3_575_808,
  entries: [
    {name: 'Themes/', directory: true, bytes: 0},
    {name: 'Themes/Ocean Breeze/', directory: true, bytes: 0},
    {name: 'Themes/Ocean Breeze/body_LZ.bin', bytes: 682_118},
    {name: 'Themes/Ocean Breeze/bgm.bcstm', bytes: 1_048_576},
    {name: 'Themes/Ocean Breeze/info.smdh', bytes: 14_016},
    {name: 'Themes/Paper Lantern/', directory: true, bytes: 0},
    {name: 'Themes/Paper Lantern/body_LZ.bin', bytes: 783_406},
    {name: 'Themes/Paper Lantern/bgm.bcstm', bytes: 1_033_676},
    {name: 'Themes/Paper Lantern/info.smdh', bytes: 14_016},
  ],
};

// The QoL ZIP vignette shows the archive from the website's capture.
export const PACK_ZIP = {
  name: 'homebrew-pack.zip',
  fileCount: 2,
  totalSize: 2_443_180,
  entries: [
    {name: '3ds/', directory: true, bytes: 0},
    {name: '3ds/3dsDL/', directory: true, bytes: 0},
    {name: '3ds/3dsDL/3dsDL.3dsx', bytes: 2_443_132},
    {name: 'README.txt', bytes: 48},
  ],
};

// --- Queue and history ----------------------------------------------------------------------

export const QUEUE_LINKS = [
  {url: 'https://github.com/BernardoGiordano/Checkpoint/releases/latest/download/Checkpoint.cia', name: 'Checkpoint.cia', host: 'github.com'},
  {url: 'https://github.com/mtheall/ftpd/releases/latest/download/ftpd.3dsx', name: 'ftpd.3dsx', host: 'github.com'},
  {url: 'https://github.com/LumaTeam/Luma3DS/releases/latest/download/Luma3DSv13.3.3.zip', name: 'Luma3DSv13.3.3.zip', host: 'github.com'},
];

export const HISTORY = [
  {name: 'sh23ds.cia', badge: 'Installed', color: 'good', time: '2026-10-20 20:08', url: DEMO_CIA_URL, where: 'Installed, file deleted: sdmc:/cias/sh23ds.cia'},
  {name: 'Checkpoint.cia', badge: 'Saved', color: 'neutral', time: '2026-10-20 19:44', url: QUEUE_LINKS[0].url, where: 'Saved as sdmc:/Checkpoint.cia'},
  {name: 'Anemone3DS.cia', badge: 'Installed', color: 'good', time: '2026-10-19 21:12', url: 'https://github.com/astronautlevel2/Anemone3DS/releases/latest/download/Anemone3DS.cia', where: 'Installed: sdmc:/cias/Anemone3DS.cia'},
  {name: 'Luma3DSv13.3.3.zip', badge: 'Resume', color: 'tag', time: '2026-10-19 20:31', url: QUEUE_LINKS[2].url, where: 'The connection dropped before the download finished.'},
  {name: 'themes.zip', badge: 'Saved', color: 'neutral', time: '2026-10-19 18:05', url: 'https://example.com/3ds/themes.zip', where: 'Saved as sdmc:/themes.zip'},
  {name: 'ftpd.3dsx', badge: 'Failed', color: 'bad', time: '2026-10-18 22:47', url: QUEUE_LINKS[1].url, where: 'The server answered 404 Not Found.'},
];
