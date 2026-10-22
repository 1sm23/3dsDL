// The Receive scene's script, in seconds from the start of the scene. The 3DS
// screens (src/screens) and the browser page (src/web) both read it, so a
// click in the browser and the console's reaction line up exactly.

export const RECEIVE_ADDRESS = 'http://3ds.local/';
export const RECEIVE_IP = 'http://192.168.1.23/';

export const SENT_LINK = 'https://github.com/BernardoGiordano/Checkpoint/releases/latest/download/Checkpoint.cia';
export const SENT_NAME = 'Checkpoint.cia';
export const SENT_BYTES = 2_386_368;

export const UPLOADS = [
  {name: 'theme.zip', bytes: 1_843_200},
  {name: 'notes.txt', bytes: 2_312},
  {name: 'Anemone3DS.cia', bytes: 1_966_080},
];

export const REMOTE_INSTALL = 'Anemone3DS.cia';
export const DOWNLOADED_FILE = {folder: 'sdmc:/luma/screenshots/', name: 'top_0001.bmp', bytes: 288_054};

export const RECEIVE = {
  duration: 20,
  // The console waits on the Receive screen while the page opens.
  pageOpens: 2.0,
  // The link is pasted into the page, then Send is clicked.
  linkPasted: 4.4,
  linkSent: 5.6,
  // The console picks it up and downloads it.
  linkReceived: 6.0,
  linkDownloaded: 9.4,
  // Three files are dropped on the page and upload one after another.
  dropStart: 10.0,
  dropped: 11.0,
  uploadsDone: 13.4,
  // Install is clicked on the uploaded CIA; the console installs it.
  installClicked: 14.0,
  installDone: 16.4,
  // A file is downloaded from the SD card to the computer.
  browseClicked: 17.0,
  downloadClicked: 18.2,
  downloadDone: 19.2,
} as const;
