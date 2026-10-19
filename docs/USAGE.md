# Using 3dsDL

## Downloading

Pick **Scan QR Code** or **Enter URL** on the home screen, check the link and folder, and press **Download**. <kbd>B</kbd> cancels at any time. If the connection drops, **Retry** continues from where it stopped, and History can resume it later, as long as the server confirms the file hasn't changed.

If a file with the same name is already there, you choose **Keep Both**, **Replace** or **Cancel**. When a CIA finishes, you can install it, install it and delete the file, or keep it; Settings can make that choice automatic. A CIA that doesn't fit on the SD card can be installed directly: it streams into the install service without being saved, and starts over if the connection drops.

Links have to point at the file itself (`http://` or `https://`). Download pages that need a browser, a login or a click won't work. To send a link from a computer, turn it into a QR code in the terminal and scan it off the screen:

```sh
qrencode -t ansiutf8 "https://example.com/some-homebrew.3dsx"
```

## Queue and Receive

**Add to Queue** on the confirm screen saves a link for later. From the scanner the camera comes straight back, so a page of codes goes quickly. **Queue** on the home screen downloads them all with **Download All**, keeping both files on a name clash and asking about CIAs once.

**Receive** listens for links while it's open. Scan its QR code with a phone on the same Wi-Fi, paste links into the page and press **Send to 3DS**. FBI's `servefiles.py`, `sendurls.py` and Boop send to port 5000 as they did with FBI.

## Files

Files browses the SD card. Tap a row to select it and tap again (or press <kbd>A</kbd>) to open it. Pictures fill the top screen, text and config files open in a line editor, ZIPs show their contents, CIAs and tickets install, and anything else opens as hex.

<kbd>X</kbd> opens the options for an entry (rename, copy, cut, delete, install, properties, SHA-256), <kbd>Y</kbd> marks several entries, and <kbd>SELECT</kbd> has the folder options (paste, new folder or file, sorting, hidden files).

## Titles

Titles lists what's installed, like Data Management in System Settings.

- **Software** and **Updates and DLC** launch, delete, and fetch a game's seed.
- **Save Data** and **Extra Data** back up, restore and browse. Backups go in `sdmc:/3ds/Checkpoint/saves/` and `.../extdata/`, named the way Checkpoint names them, so either app restores the other's. A restore backs up the current data first.
- **Tickets** exports a title's ticket to `sdmc:/3ds/3dsDL/tickets/` or deletes unused ones. Install a `.tik` from Files.
- **Clean Up** removes unfinished installs, which stop a CIA from installing again, and **Seeds** imports a `seeddb.bin`.

System titles never appear, so they can't be deleted from here.

## Controls

| Where | Button | Action |
| --- | --- | --- |
| Everywhere | <kbd>A</kbd> / <kbd>B</kbd> | Choose / back |
| Home | <kbd>START</kbd> | Exit |
| Lists | <kbd>L</kbd> / <kbd>R</kbd> | Page up / down |
| Files | <kbd>X</kbd> / <kbd>Y</kbd> / <kbd>SELECT</kbd> | Options / mark / folder options |
| Text editor | <kbd>A</kbd> / <kbd>X</kbd> / <kbd>Y</kbd> / <kbd>START</kbd> | Edit line / new line / delete line / save |
| Pictures | <kbd>L</kbd> / <kbd>R</kbd> | Previous / next picture |

Everything also works by touch, and lists scroll with a flick.

## Settings

| Setting | Default | |
| --- | --- | --- |
| Resume interrupted downloads | On | Keeps partial downloads to continue later |
| Stay awake while downloading | On | Pauses Sleep Mode so closing the lid doesn't stop Wi-Fi |
| Offer to extract ZIP files | Off | Asks to extract a ZIP after it downloads |
| After a CIA downloads | Ask | Ask, install, install and delete, or keep |
| Check for updates | On | Looks for a new 3dsDL when it opens |
| Sounds | On | Needs `sdmc:/3ds/dspfirm.cdc` |

**Console Clock** compares the console's time with internet time and sets it for your time zone. HTTPS fails when the date is wrong. Setting the clock needs the CIA build; Rosalina (<kbd>L</kbd> + <kbd>Down</kbd> + <kbd>SELECT</kbd>, Miscellaneous options) can also set it.

## Reporting a problem

**Settings → Report Issue** (or **About → Report an Issue**) shows a QR code for each place to get help. Scan it with a phone:

- **Report a Bug** opens the [bug report form](https://github.com/ankushKun/3dsDL/issues/new?template=bug.yml) with your 3dsDL version, CIA or 3DSX, console model and system version already filled in.
- **Discord Server** is an [invite](https://discord.gg/axrWTjyAWG) to the developer's server, for questions, ideas and chatting with other users.
- **Developer on X** is [@ankushKun_](https://x.com/ankushKun_), for quick questions or if you don't use GitHub.

A good bug report says what you did, what happened instead, and the exact error on screen. Rosalina takes screenshots of both screens (<kbd>L</kbd> + <kbd>Down</kbd> + <kbd>SELECT</kbd>, Take screenshot) into `sdmc:/luma/screenshots/`. Reports of how 3dsDL runs on an Old 3DS or 2DS are welcome as a [console test report](https://github.com/ankushKun/3dsDL/issues/new?template=console-test.yml).

## Files 3dsDL keeps

| Path | |
| --- | --- |
| `sdmc:/3ds/3dsDL/settings.ini` | Settings |
| `sdmc:/3ds/3dsDL/history.txt` | The last 50 downloads |
| `sdmc:/3ds/3dsDL/seeds/<title id>.dat` | Seeds to use before asking Nintendo's server (FBI's `sdmc:/fbi/seed/` works too) |
| `<folder>/.3dsdl-<id>.part`, `.resume` | A partial download, removed once it finishes |

## Building

`./scripts/build.sh` builds in Docker (Docker Desktop, or Colima on macOS) with a digest-pinned devkitPro image and writes `3dsDL.3dsx`, `3dsDL.cia`, `3dsDL.smdh` and their checksums to `dist/`. The 3DSX is reproducible byte for byte; the CIA differs only in the IDs makerom randomises.

| Variable | Effect |
| --- | --- |
| `THREEDSDL_BUILD_JOBS=4` | More compiler jobs |
| `THREEDSDL_FLAT_BANNER=1` | Flat HOME Menu banner instead of the 3D one |
| `THREEDSDL_BUILD_IMAGE=...` | Another devkitPro image |
| `THREEDSDL_CIA_IMAGE=...` | Another CIA packaging image |
| `THREEDSDL_KEEP_COLIMA=1` | Leave Colima running |

Without Docker, install devkitARM, libctru, Citro2D, Citro3D, 3ds-curl and 3ds-mbedtls with devkitPro's pacman and run `make`.

The host tests (`make -C tests check`) need a C++17 compiler, libcurl and zlib headers, Python 3 and OpenSSL. They run under AddressSanitizer and UBSan against a local HTTP, HTTPS and NTP server. Pushing a tag that matches `VERSION` makes CI attach the builds to a draft release.

| Folder | |
| --- | --- |
| `source/app/` | 3DS code: screens, UI toolkit, scanner, workers |
| `source/core/` | Portable logic, tested on the host |
| `source/net/` | Download engine, fetch and the link receiver |
| `tests/` | Host tests and the fixture server |
| `site/` | The website |
