# Hardware test plan

The host tests cover the download engine, file naming, ZIP extraction and QR
decoding, but nothing in them touches the camera, the GPU, the software
keyboard, Wi-Fi or sleep mode. This checklist covers those parts on real
consoles. Run it on at least one Old and one New 3DS before tagging 1.0, and
record each run in the matrix at the bottom.

Use both builds when you can: `3dsDL.3dsx` from the Homebrew Launcher and
`3dsDL.cia` from the HOME Menu. They differ in memory layout, service access
and (on New 3DS) CPU speed.

## Preparation

- A computer on the same network running the fixture server:
  `python3 tests/fixtures/server.py --cert cert.pem --key key.pem --ready ports.json`
  (see `tests/run_integration.sh` for how to make a throwaway certificate).
  Its `/file/<size>` endpoint takes the query options listed at the top of
  `server.py`, which makes drops, stalls and slow transfers easy to trigger.
- A few QR codes, for example made with
  `qrencode -t ansiutf8 "https://…"` and shown on a monitor:
  a short `https://` link, a long link (around 200 characters), an upper-case
  `HTTPS://EXAMPLE.COM/FILE.ZIP` link, and a QR code that is not a link
  (a Wi-Fi code or plain text).
- A small ZIP with a folder inside, and a large file (at least 50 MiB).

## Checklist

### Start-up and home screen

1. The app starts without a "Startup failed" dialog.
2. The top screen shows the download folder, the free space and the Wi-Fi
   state. Turning Wi-Fi off in the HOME Menu settings changes it within a
   couple of seconds of returning.
3. D-Pad, Circle Pad and touch all move between and open the six buttons.
   START exits.
4. Settings from 0.2 (`sdmc:/3dsDL.cfg`) are migrated to
   `sdmc:/3ds/3dsDL/settings.ini` and the old file is removed.

### QR scanner

5. The preview is upright, not mirrored, with natural colours, and keeps
   updating for at least two minutes (watch for it freezing).
6. Each test code is read. The non-link code shows "That QR code does not
   hold a web link" and scanning continues.
7. Close the lid while scanning, open it again: the preview recovers, or a
   clear "camera stopped" error appears (no hang).
8. Press HOME while scanning, return: the preview recovers.
9. B, START and the CANCEL button all leave the scanner.
10. Scan, cancel, then open ENTER URL: the keyboard works and nothing on
    screen is corrupted afterwards.

### Downloads

11. The confirm screen shows the host, the full link (wrapped), the folder and
    the http warning for plain-http links.
12. A large download (several MiB, from GitHub for example) shows speed, time
    left, size and percentage, and its status says "4 connections". The
    finished message gives the average speed; write it in the matrix. Then
    check the file is intact: a CIA installs with FBI, a video plays to the
    end.
13. Cancelling asks first; "KEEP GOING" continues, "STOP" removes the partial
    file.
14. Close the lid during a download with "Stay awake" on: the download keeps
    going. With it off: the download fails and offers RETRY.
15. Use `drop_at` on the fixture server with a file under 1 MiB, which uses
    one connection: the failure says the connection dropped and RETRY
    continues from the partial size ("Continuing from…"). With a larger file
    the dropped connection is reopened and the download simply finishes.
16. Press HOME during a download, then close 3dsDL from the HOME Menu. Start
    it again: History lists the download as resumable and RESUME continues it.
    Try it once with a large download split across connections too, and
    check the finished file as in item 12.
17. Download the same file twice: RENAME creates `name (1).ext`, OVERWRITE
    replaces it, CANCEL keeps the old file.
18. With Wi-Fi off, starting a download warns first.
19. Set the console clock to 2010, then download over HTTPS: the error says
    the certificate is not valid yet, shows the console's time and offers
    CHECK CLOCK.
20. Settings → Console clock shows the console time, the actual time and the
    drift. TIME ZONE previews the local time for each zone. SET CLOCK sets it
    (CIA build) or explains the Rosalina alternative when refused (3dsx).
    Afterwards the HOME Menu clock matches and HTTPS works again.

### Folders, history, settings, ZIP

21. Presets create their folder when it is missing. The browser can open
    folders, go up, make a new folder (X) and choose a folder (Y).
22. Typing a path outside `sdmc:/` is refused; a missing folder offers to be
    created.
23. History shows saved, failed and cancelled entries with the right details;
    REMOVE and "Clear all" work and survive a restart.
24. With "Offer to extract ZIP files" on, a ZIP download offers HERE / NEW
    FOLDER / NO, asks about existing files, shows progress, and DELETE ZIP
    removes the archive. With it off, nothing is offered.

### HOME Menu banner and icon

Release builds use the animated 3D banner (see `resources/banner/README.md`).
A bad one crashes or freezes the HOME Menu, so check these on a console
whenever the banner changes. `THREEDSDL_FLAT_BANNER=1` builds with the flat
picture instead.

25. After installing the CIA, the HOME Menu shows 3dsDL's icon (a cyan card
    with a yellow arrow). The Homebrew Launcher shows the same icon for the
    `.3dsx`.
26. Selecting 3dsDL plays the jingle once and shows the banner: the arrow
    drops into the card, the card squashes, sparkles burst and the wordmark
    bobs. It loops without a jump, and nothing is black or flickers.
27. With the 3D slider up, the wordmark and sparkles stand in front of the
    screen and the card sits behind them.
28. Moving the cursor on and off 3dsDL several times, and opening and closing
    the app, never crashes or freezes the HOME Menu. If it does, hold POWER
    to switch off if needed, then open FBI without highlighting 3dsDL and
    install a flat banner build (`THREEDSDL_FLAT_BANNER=1`) over it. If the HOME Menu starts
    with 3dsDL already highlighted and crashes again, starting once without
    the SD card should move the cursor elsewhere.

### Installing CIAs

These need a few CIAs: a small homebrew app (3dsFM works), a retail game
from 2015 or later that uses a seed, an update and a DLC for a game that is
installed, and a game older than the installed version.

29. Downloading a `.cia` offers INSTALL, INSTALL & DELETE and KEEP FILE with
    the icon, name, version and installed version. INSTALL installs it and
    the title appears on the HOME Menu; INSTALL & DELETE also removes the
    file. The "After downloading a CIA" setting skips the question.
30. Files lists folders and files, shows a CIA's details on the top screen and
    installs or deletes it. B goes up and lands on the folder just left.
31. A game that needs a seed starts after installing (the success message says
    where the seed came from). Try it once with
    `sdmc:/3ds/3dsDL/seeds/<title id>.dat` and once from Nintendo's server.
32. Installing the same version again works, or asks to replace it. Note
    which one, and whether the save survives REPLACE. An older version asks
    first and warns about save data.
33. An update and a DLC install over their game, and LAUNCH opens the game.
34. Cancelling halfway leaves nothing behind: the title is not on the HOME
    Menu and installing it again works.
35. A CIA bigger than the free space is refused before anything is written.
36. LAUNCH after installing starts the title, from both the `.3dsx` and the
    CIA build. Installing 3dsDL's own CIA over itself from the CIA build works.

### Files

Use a folder with a few of each: big phone photos (JPEG over 12 MP), a PNG
with transparency, a GIF, `.ini` and `.txt` files with Windows line endings,
a ZIP, a `.3dsx`, a decrypted and an encrypted `.3ds`, an `.nds` and a folder
with a few hundred files.

37. Scrolling a folder of a few hundred files stays smooth on an Old 3DS, and
    the top screen keeps up with the highlighted entry.
38. Pictures open, fit the screen, and L/R browse the folder. A 12 MP photo
    opens in a few seconds without running out of memory, on Old and New.
39. Editing a line of an `.ini` and saving changes only that line (compare
    the file on a computer); a new file gets a line break at the end. B with
    unsaved changes asks first. The keyboard shows the line's current text.
40. Copy and paste a folder of a few hundred MiB: progress moves, cancel
    stops it and leaves the finished files, and the copy matches the
    original. Paste into the same folder makes "name (1)".
41. Cut and paste moves instantly, also between folders, and never into
    itself. Something that could not move stays ready to paste.
42. Delete a folder with files inside: it says how much is inside first, then
    removes everything.
43. Rename, including changing only the case of a name (a.txt to A.txt).
44. Mark several CIAs and install them: each installs, and the summary counts
    them.

## Results so far

On 2026-10-12 the 0.3.0 build was confirmed on a console for downloads over
the internet (items 11 and 12), installing the CIA with FBI, and ZIP
extraction (item 24). The console model was not recorded, so the matrix
below is still open.

On 2026-10-19, before any console run, the CIA installer was tried in Azahar
2126.2 with the `.3dsx`: install from Files, download then INSTALL & DELETE
from a local server, same-version reinstall, History marking the download as
installed, and LAUNCH (items 29, 30 and 36 in part). Azahar's AM is more
forgiving than a console's, so items 31 to 35 still need hardware.

On 2026-10-18 a test build of 0.4.0 (pre-release `test-fast-downloads-1`)
downloaded a 38 MB video over 4 connections at an average of 500 to 600
KiB/s (item 12). The single-connection speed of 0.3.2 on the same file and
network was not measured, so the gain is not known exactly.

The same day, the first 3D banner build (commit 539b3d8) crashed the HOME
Menu every time 3dsDL was selected (item 28). Its CGFX left every mesh's node
name blank; `build-cgfx.py` now fills them in, and the flat banner is the
default until a 3D build passes items 26 to 28. The second 3D build (with the
node names) still crashed: Luma reported a data abort in `menu` (HOME Menu)
reading address 0x8 through a null pointer. Its bone lookup tree was stale
after pycgfx moved the wordmark's bone last, so lookups for five bones landed
on the wrong entries. `build-cgfx.py` now rebuilds the tree and checks every
lookup. The third 3D build (commit 155e8a5) got further: the banner drew its
first frame correctly, then the HOME Menu froze (no input, music still
playing). That build was also the first with a blended wordmark marked
translucent, parts scaled to 0.001 and 22.05 kHz sound; the fourth reverts
all three, and a still version of it (no animation) was built alongside to
tell an animation problem from anything else. The fourth, animated
(test-banner-5), worked: no crash or freeze, and the banner animates. Its one
flaw was the wordmark vanishing when seen from behind, so the wordmark is now
a billboard that always faces the viewer. That build (test-banner-7, commit
c9bd54f) passed: the banner animates, the wordmark stays visible,
and the HOME Menu neither crashes nor freezes. It became the default for
0.3.1.

## Compatibility matrix

| Console | System version | Luma3DS | Build | Checklist | Download speed | Tester, date | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| New 3DS XL | | | 3dsx | not run | | | |
| New 3DS XL | | | cia | not run | | | |
| Old 3DS / 2DS | | | 3dsx | not run | | | |
| Old 3DS / 2DS | | | cia | not run | | | |

Mark the checklist column with the item numbers that failed, or "all pass".
