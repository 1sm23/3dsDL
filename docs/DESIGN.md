# 3dsDL System Look

The design language from 0.7 on. The mockups for every screen are on the
design canvas at https://claude.ai/artifact/G2ZpjCidUfCz82fapdc1Ke (private
until shared). This file is the version the code follows. All sizes are
console pixels; the canvas draws the screens at 2x.

## The goal

3dsDL should look and behave like software that came with the console. The
reference is the family of system applets: System Settings and Data
Management first, then the error and confirmation dialogs, the HOME Menu
status bar and the green selection cursor. Values below were sampled from
real captures of those applets, not from memory.

3dsDL keeps its own identity the way each system app does: one accent colour
(3dsDL blue, `#1C93C2`, where Data Management uses green) and its own icons.
No Nintendo logos, mascots or artwork are used. On the console the type is
the system font itself, which is what makes it read as native.

## Rules

1. **The top screen explains, the bottom screen does.** Titles, previews,
   details and help text go up top. Everything interactive is on the bottom
   screen. Nothing on the top screen responds to input.
2. **Few, large controls.** Embossed cream buttons 26 to 40 px tall, at most
   four or five choices per screen. Long lists page through a recessed panel
   with a page counter instead of scrolling.
3. **Back and OK live in the bottom corners.** Grey tabs, Back (or Cancel or
   Close) on the left, the screen's main action on the right. The home screen
   has one full-width Close bar instead.
4. **The green ring is the D-pad.** Touch and buttons work everywhere. The
   ring marks where A goes. A chosen option turns yellow with orange brackets.
5. **Questions are system dialogs.** A cream box over the dimmed bottom
   screen with a gold button strip. Destructive questions get a red button
   and a dark Cancel strip.
6. **Words like the system's.** Title Case on buttons ("Install and Delete
   File"), sentences in panels, "Please wait..." while busy, no jargon in the
   first line of an error; the technical detail goes on the top screen.

## Tokens

### Colour

| Role | Value |
| --- | --- |
| Top screen | vertical `#F2EBB2` to `#F2E9A3` |
| Bottom screen | vertical `#F1E9A3` to `#F0E694` |
| Info panel | `#DCD49A`, radius 5 |
| Recess (lists, option groups) | `#D9D3A0`, radius 8, 1 px inner shadow |
| Dotted divider | dots `#B0A66E`, about 2.5 px, 8 px apart |
| Ink | `#615A4B` panels, `#756E60` buttons, `#141414` dialogs, `#8C8466` secondary |
| 3dsDL blue | `#1C93C2` titles and the app's icons |
| Focus ring | `#3CDC78`, 3 to 4 px, glow `#A0FAA0` |
| Label tag | `#609CB4`, white text; green `#3FA65A` for good news |
| Count badge | `#E69614`; green, red `#E0301A`, grey `#ACA27A` variants |
| Progress | track `#CFC795`, fill `#FFCC22` to `#FF9900` |
| Nav tab | `#787878` to `#686868`, 2 px top highlight `#C4C4C4`, white text |
| Dialog | edges `#FFFDF0`, centre `#E4E3DD`; strip `#FFFDF2`, `#FFF0B4`, `#FAE29A`, `#AE8D67`; dividers `#555553` and `#191817` |
| Dim | `rgba(20,18,8,.48)` |

### Buttons

| Kind | Fill | Edge | Text |
| --- | --- | --- | --- |
| Normal | `#FEFCF5` to `#E0DCCB`, darkening to `#B6B192` over the last 12 px | 1 px `#B3AA83` sides, `#8F8659` bottom, white top highlight, 3 px soft shadow | `#756E60` |
| Chosen | `#FFED7F` to `#C9BF49` | orange brackets `#E89436` at both ends | `#E07A10` |
| Current | `#80B6C8` to `#557E86` | `#4E7A86` | white |
| Destructive | `#FE3014` to `#B31B0B` | `#9A1A0C` | white |
| Disabled | none | 1.5 px dashed `#CFC48A` | `#CFC48A` |

Radius 9 for big buttons, 8 small, 10 for menu tiles. Pressed: 10% darker
and 1 px lower; actions fire on release.

### Type (system font)

| Role | Cap height | Use |
| --- | --- | --- |
| Hero | 19 | the app name on Home |
| Title | 16 | the title row on the top screen |
| Button | 15 | big buttons, nav tabs (14) |
| Body | 13 | panels and dialogs, 22 px line pitch |
| Row, meta | 10 | list rows, sizes, tags |
| Status | 11 | the clock |

Calibrate citro2d scales against these cap heights on the console; the
first guess is cap 15 at scale 0.72.

## Layout

### Top screen

- Status bar, y 0 to 17: Wi-Fi bars at x 3, the connection pill at x 27
  ("Connected" in blue, "Not Connected" in grey), date and time right-aligned
  to x 362, battery at x 371.
- Title row, y 24 to 50: icon and title centred, in 3dsDL blue.
- Dotted divider at y 57.
- Info panel at x 17 to 383, y 69 to 225. Content screens (text editor, hex
  view, pictures) use the whole area below the title row instead.

### Bottom screen

- Content from y 6 to 202.
- Nav tabs at y 209 to 239, 126 px wide, the inner top corner rounded 22.
- A page counter ("1/3") sits between the tabs at the bottom right, with a
  small label to its left.

### Lists

A recessed panel holds rows 27 px tall on a 31 px pitch: white pills with a
17 px icon, the name and a right-aligned size or badge. Six rows fit; L/R and
the counter page through the rest. Marked rows turn light blue with a Marked
badge.

### Dialogs

Box at x 10, y 12, 300 by 216, radius 13, over the dimmed bottom screen. The
message is centred in body type. The strip is 39 px with one to three
buttons; the focused one carries the green ring. Destructive dialogs swap the
strip for a red button and a 30 px dark Cancel strip.

## Screens

| Board | Top screen | Bottom screen |
| --- | --- | --- |
| Home | app icon, name, folder and free space | Settings and Queue bars, six tiles (Scan QR Code, Enter URL, Receive; History, Files, Titles), Close bar |
| Scan QR code | camera with corner brackets, status strip at the bottom | title, help panel, Type a URL Instead, Back |
| Confirm download | file, host, URL, Save to and HTTPS tags | folder recess, Change Folder, Cancel / Download |
| Downloading | file, progress, amounts, speed, connections tag | saving to, Sleep Mode note, Cancel |
| File exists | which file and what each choice does | dialog: Keep Both / Replace / Cancel |
| Install prompt | icon, name, publisher, Title ID, Version, Size | Install, Install and Delete File, Keep File |
| Installing | icon, name, progress | do not turn off, Cancel |
| Installed | icon, name, time taken | dialog: Close / Launch |
| Files | details of the highlighted entry | path, six rows, Back, page counter, Options |
| File options | what the highlighted choice does | eight small buttons in two columns |
| Picture viewer | the picture, nothing else | name, size, position, Previous / Next |
| Text editor | ruled page with line numbers, current line in blue | the full current line, Edit / New Line / Delete, Close / Save |
| Hex view | offset, bytes, characters | offset, page buttons |
| ZIP contents | totals and the selected entry | entries, Extract Here / Extract to New Folder |
| CIA details | icon, name, Title ID, Version, Size | Install, Install and Delete File, Delete File |
| Delete | what goes and how big it is | destructive dialog |
| History | the highlighted download in full | rows with Installed, Resume, Saved, Failed badges |
| Settings | what is in the highlighted section | About and Updates bars, four tiles |
| Setting choice | what the setting does | recessed options, the current one chosen |
| Console clock | console time, internet time, zone | Set the Clock, Change Time Zone |
| Download folder | current and selected folder | presets and recent folders |
| Error | what was kept, the technical detail | dialog: Close / Retry |
| Please wait | what is happening | small dialog with the spinner |

### Notifications

News that is not a question, such as a new version of 3dsDL, slides down
from under the status bar over whatever the top screen shows, like the HOME
Menu's notifications. A card 336 by 50 at y 22: white to cream, radius 12, a
48 px app-coloured square with a white icon, a title in small type and one
line under it. A thin line under the text runs out over the six seconds it
stays, then the card slides back up. It waits while a dialog is open. The
control it points to (Settings, then Updates) carries an orange "New" badge
on its top right corner, drawn over the focus ring.

## Motion and sound

- The focus ring glides to its target in 80 ms; its glow breathes between
  60% and 100% over 1.2 s.
- Dialogs dim the screen in 120 ms and scale from 92% to 100% in 140 ms.
- Screens cross-fade in 100 ms. Progress bars ease.
- Optional soft clicks for move, select and back, off by default.

## How the code draws it

`source/app/ui.cpp` is the toolkit; every screen uses it.

- **Shapes are textured quads.** `scripts/make-ui-icons.py` renders the
  icons and, in the last row of the same atlas (`romfs/ui/icons.png`), a
  white disc and four rings. A rounded rectangle is plain bands split at its
  gradient stops plus four anti-aliased quarter discs; a ring is straight
  bars plus quarter rings. Colours, gradients included, are per vertex, so a
  whole screen of shapes is one batch.
- **No proctex.** citro2d tints images through procedural textures, which
  Azahar does not emulate (the same reason its circle primitive draws
  nothing there). The toolkit sets a text-style combiner instead: colour
  from the vertex, coverage from the texture. Real pictures go through
  `ui::drawImage`, which lets citro2d set up its own combiner again.
- **Dialogs dim the real screen.** `choose` copies both framebuffers into
  textures when it opens and draws the box over a dimmed copy.
- **Touch is on release.** Buttons and tabs highlight while pressed and act
  when the stylus lifts over them, as the system does.
- **Scrolling.** `ui::Scroller` drags, flicks with momentum and springs back
  at the ends; lists clip their rows with the scissor test. The text
  editor and hex view scroll from a drag anywhere on the touch screen
  outside their buttons, and pictures change with a sideways swipe.
- **Long press** on a Files row opens its options.

In Azahar the busiest screens draw in under 2 ms of CPU a frame per screen.
The console pass still has to confirm 60 frames a second on an Old 3DS,
the text sizes against real captures, and the focus ring's thickness.

## Building it (done in 0.6)

1. **Drawing primitives.** Rounded rectangles from rectangles and corner
   triangle fans (the emulator skips citro2d's circle primitive), vertical
   gradients from per-corner colours, soft shadows as stacked translucent
   rounded rects, the dotted divider, the focus ring with its glow.
2. **Icon atlas.** The icons on the canvas exported to PNG at 17, 22 and
   30 px, packed into one `.t3x` with tex3ds under `gfx/`, loaded from
   romfs.
3. **Components.** Status bar (Wi-Fi strength, clock, battery from PTMU),
   title row, info panel, button and tile, nav tabs, recessed list with
   paging, label tag and badge, progress bar, dialog with strip and the
   destructive variant, the spinner.
4. **Screens.** Port each board in the order of the table, checking each in
   Azahar against the canvas.
5. **Calibration on a console.** Text scales, the focus ring thickness and
   the button gradient read differently on the real LCDs.
