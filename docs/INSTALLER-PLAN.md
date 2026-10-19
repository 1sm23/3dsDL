# Plan: CIA installing, a file browser, and the rest of FBI

FBI and its forks are no longer maintained, and r/3dsqrcodes is now pointing
people at 3dsDL. The two things they need first are installing a CIA after it
downloads and a file browser that shows files, not only folders. This plan
covers those two, then works toward everything else FBI and FBI Reloaded did.

Research notes come from reading Steveice10/FBI 2.6.1, TheRealZora's FBI
Reloaded and colbyshores' FBI Reloaded 1.5.0. All three are MIT, so code can
be adapted into 3dsDL (GPLv3) as long as the copyright and permission notice
is kept, the same way quirc is handled in `LICENSE-quirc.md`.

FBI Reloaded adds very little over FBI: a build-time light or dark theme, a
100 s HTTP timeout, renamed strings and one extra AM error message. The 1.5.0
fork adds libcurl downloads and the 48K socket buffers that 3dsDL already
has. So "FBI Reloaded features" really means FBI's features, and the things
users ask for on top of that are quality of life, covered in phase 4.

## Design decisions

**Download to the SD card, then install.** FBI streams the HTTP body straight
into the AM install handle and never saves the CIA. 3dsDL should not copy
that by default, because the AM handle needs the data in order, and 3dsDL's
speed comes from four connections writing different parts of the file at
once. Saving first keeps the split download, resume after a dropped
connection, and retry from History. Installing from the SD card afterwards is
quick compared to the download. "Install and delete" removes the CIA once
the install succeeds, so the user ends up in the same place as with FBI.

The cost is that the CIA and the installed title briefly sit on the card
together. Before downloading a `.cia`, 3dsDL already checks free space; once
the header has arrived it can also call `AM_GetCiaRequiredSpace` and warn
early. A streaming "direct install" mode that uses one connection is planned
for phase 4, for people whose cards are nearly full.

**Services.** The installer needs `am:net`. The CIA build adds it to
`ServiceAccessControl` in `resources/3dsDL.rsf`, plus `SeedDB` and
`CreateSeed` in `FileSystemAccess` for seeds. The `.3dsx` build gets every
service under Luma3DS, which is what everyone running homebrew today uses, so
FBI's kernel backdoor PID patch is not needed. If `amInit()` fails, the
install buttons say why instead of appearing broken. 3dsDL calls
`AM_InitializeExternalTitleDatabase(false)` once at start, as FBI does, so a
card without `title.db` still works.

**Reinstalling a title that is already installed.** FBI deletes the old
title and its ticket before every install, without comparing versions. That
allows downgrades, but it is unclear whether it also wipes the save on the
SD card, and losing saves would be the worst bug this feature could have.
3dsDL will start the install without deleting anything. AM accepts the same
or a newer version over the top, and updates and DLC install alongside the
base game. Only when AM refuses with "outdated title version" (0xD8E08027)
does 3dsDL ask whether to remove the installed copy first, with a warning
about save data. Whether `AM_DeleteTitle` removes the save gets checked on a
console before that dialog's wording is final.

**Where the code goes.** The new screens went into `app.cpp` with the others for
phase 1; phase 2's file manager is big enough to move into its own file.

| File | What it holds | Host tests |
| --- | --- | --- |
| `source/core/cia.cpp` | Reads a CIA header: title ID and version from the TMD, the SMDH (name, publisher, 48x48 icon) from the meta section, New 3DS only flag | Yes, against small synthetic CIAs |
| `source/core/ticket.cpp` (phase 2) | Title ID from a `.tik` | Yes |
| `source/app/installer.cpp` | `AM_StartCiaInstall`, writes, `AM_FinishCiaInstall` or `AM_CancelCIAInstall`, tickets, seeds, error text for AM results | No (libctru) |
| `source/core/smdh.cpp` | Name, publisher and icon from an SMDH block, shared with `.3dsx` files later | Yes |
| `source/app/app.cpp` | The install prompts and the Files screen, next to the other screens | Listing yes (`fs::listEntries`), screens no |
| `source/app/titles.cpp` | Installed titles, tickets, pending titles (phase 3) | No |

`tasks::install` runs on the worker thread the same way `tasks::download`
does, so the progress screen, cancel button and keep-awake behaviour are
shared.

## Phase 1 (0.5.0): install after download

This is what was promised on Reddit and should ship first. Items 1 to 6 are
built and were tried in Azahar on 2026-10-19; the console pass in item 7 is
still open. A basic Files screen (list, CIA details, install, delete) came
forward from phase 2 so CIAs downloaded with older versions can be installed
too.

1. `core/cia` parser and its unit tests. A CIA's meta section is optional, so
   a CIA with no icon still installs and just shows its title ID.
2. `installer` with FBI's sequence: pick SD or NAND from the title ID the way
   `fs_get_title_destination` does, warn when a New 3DS only title is
   installed on an Old 3DS, `AM_StartCiaInstall`, write in 256 KB blocks,
   `AM_FinishCiaInstall`, then fetch the seed (see phase 3) and ignore a
   failure there. If the title is a firmware title, call `AM_InstallFirm`
   exactly as FBI does, though 3dsDL should refuse system titles by default.
3. After a `.cia` finishes downloading, a dialog shows the icon, name, title
   ID, version and size with three buttons: INSTALL, INSTALL & DELETE and
   KEEP FILE. A new setting, "After downloading a CIA", can make that choice
   automatic so QR users get scan, download and install with no prompts.
4. After a successful install, offer LAUNCH using
   `APT_PrepareToDoApplicationJump` and `APT_DoApplicationJump`. FBI does
   not have this, and it answers the Reddit question about where a
   downloaded game went: it is now on the HOME Menu, and can be opened from
   here.
5. History entries record "Installed" alongside Saved, Failed and Cancelled,
   and installing a CIA from a History entry works when the file is still
   there.
6. AM error messages translated into words, taken from FBI's
   `core/ui/error.c` table (wrong installation state, invalid NCCH, outdated
   version, not enough space, signature check failed).
7. rsf permissions, the `amInit` check, README, ROADMAP, the site, and a
   hardware pass on an Old and New 3DS: install to SD, an update over a base
   game, a DLC, a reinstall of the same version, install and delete, cancel
   halfway (the title must not show up half-installed), and a full SD card.

## Phase 2 (0.6.0): the file browser

Built on 2026-10-19 and tried in Azahar: everything below, plus a line editor for text files, new files, sorting,
and titles and icons for `.3dsx`, `.3ds`, `.nds` and `.tik` files. JPEGs are
decoded at a reduced size with libjpeg-turbo so phone photos open on a
console; other pictures use stb_image and are limited to 6 megapixels.

1. A Files screen on the home menu that lists folders and files in
   `sdmc:/`, with sizes, sort by name, size or date, and remembers the
   last folder.
2. File details on the top screen. A CIA shows its icon, name, publisher,
   title ID, version, region, size and whether it is already installed (and
   which version). A `.3dsx` shows its icon and name from its SMDH. A
   `.tik` shows its title ID. Icons are decoded off the main thread and
   cached so scrolling stays smooth on an Old 3DS.
3. Actions on a file: Install CIA, Install & delete, Install ticket,
   Rename, Delete, Copy, Cut, Paste. On a folder: New folder, Rename,
   Delete, Install all CIAs, Install & delete all CIAs, Delete all CIAs.
4. Multi-select with Y, so several CIAs can be installed in one go with one
   progress screen that shows "3 of 7".
5. Previews: images (PNG, JPG, BMP), plain text and `.ini` or `.json`, the
   file list inside a ZIP (reusing `core/zip`) with Extract, and a hex view
   for anything else.
6. Hidden files toggle. Recycle bin is out of scope; Delete always asks.

The NAND mounts FBI offers (CTR NAND, TWL NAND, photo, sound) are left out
on purpose. Writing to them can brick a console, the r/3dsqrcodes audience
does not need them, and GodMode9 does that job better. They can be added
later as read-only views if someone asks.

## Phase 3 (0.7.0): titles, tickets and seeds (done, see app/titles_screens.cpp)

1. Titles screen: every installed title on SD and NAND with icon, name,
   title ID, version and size, sortable, with Launch, Delete title, Delete
   title and ticket. System titles are hidden unless a setting shows them,
   and they cannot be deleted.
2. Tickets screen with Delete and Delete unused, and Pending titles (failed
   or interrupted installs) with Delete, which clears the stuck entries
   that make reinstalls fail.
3. Seeds. Newer games need a seed to start. FBI asks Nintendo's
   `kagiya-ctr.cdn.nintendo.net` for it and adds it with `FSUSER_AddSeed`.
   That server may not survive the eShop shutdown, so 3dsDL also reads
   `sdmc:/3ds/3dsDL/seeds/<titleid>.dat` and a `seeddb.bin`, and can
   download a `seeddb.bin` from a configurable URL. "Import seed" is on the
   Titles screen for games installed elsewhere.
4. Ext save data and system save data lists with Delete, as FBI has. Save
   import and export over SPI for DSiWare is left out; Checkpoint does it.

## Phase 4: beyond FBI

These are the things users ask for that no FBI build ever had.

1. Queue: scan several QR codes in a row, see them as a list, and let them
   download and install one after another. A "Scan again" button after each
   install.
2. Receive from the network. FBI's remote install listens on port 5000 for a
   4-byte length followed by newline-separated URLs, and the existing
   `servefiles.py` and `sendurls.py` scripts send to it. 3dsDL will accept
   the same protocol so those tools keep working, and also serve a small web
   page so a phone on the same Wi-Fi can paste a link. This was already on
   the roadmap.
3. Direct install for nearly full SD cards: one connection, data written
   straight into the AM handle as FBI did, with no file saved and no resume.
4. Self update: check GitHub releases, download the new 3dsDL, and install
   it. Installing the running CIA over itself needs FBI's special case
   (skip the delete, finish, then exit).
5. SHA-256 of finished downloads, already on the roadmap.

## Testing

Azahar/Citra implements the AM install calls, so most of phase 1 can be
driven in the emulator before a console run. The CIA parser and ticket
parser get unit tests in `tests/unit`, built from small synthetic CIAs that
the test writes itself so no copyrighted data lands in the repo. The
integration server can serve a CIA so the download then install path is
covered up to the AM call. Everything that touches AM, seeds or launching is
checked on a console and added to `docs/HARDWARE.md`.
