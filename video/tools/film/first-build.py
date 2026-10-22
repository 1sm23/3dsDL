#!/usr/bin/env python3
# The script that wrote the first build of "Follow the ring": index.html,
# compositions/frames/*.html and tools/score/cue-sheet.ts, all from one set
# of timings. Kept for reference. Running it again OVERWRITES those files and
# any hand edits in them, so edit the HTML directly instead unless you mean to
# rebuild everything from here.
import json, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FR = os.path.join(ROOT, "compositions/frames")
os.makedirs(FR, exist_ok=True)

# ---------------------------------------------------------------- icons
ICONS = {
    "Scan": '<path d="M4 9V5a1 1 0 0 1 1-1h4M15 4h4a1 1 0 0 1 1 1v4M20 15v4a1 1 0 0 1-1 1h-4M9 20H5a1 1 0 0 1-1-1v-4"/><rect x="8" y="8" width="3" height="3"/><rect x="13" y="8" width="3" height="3"/><rect x="8" y="13" width="3" height="3"/><path d="M13 13h3v3"/>',
    "Globe": '<circle cx="12" cy="12" r="8"/><path d="M4 12h16M12 4c2.4 2.3 3.5 5 3.5 8s-1.1 5.7-3.5 8c-2.4-2.3-3.5-5-3.5-8s1.1-5.7 3.5-8z"/>',
    "History": '<path d="M4.5 12a7.5 7.5 0 1 0 2.2-5.3"/><path d="M4 4.5v3.5h3.5"/><path d="M12 8v4.5l3 2"/>',
    "Folder": '<path d="M3.5 7.5a1.5 1.5 0 0 1 1.5-1.5h4l2 2h8a1.5 1.5 0 0 1 1.5 1.5v8a1.5 1.5 0 0 1-1.5 1.5H5a1.5 1.5 0 0 1-1.5-1.5z"/>',
    "Wrench": '<path d="M14.5 6.5a4 4 0 0 0 4.8 5.2L12 19l-2.5.5L9 17l7.3-7.3"/><path d="M14.5 6.5L17 4a4 4 0 0 1 3 4.7"/>',
    "Download": '<path d="M12 4v10M8 10l4 4 4-4"/><path d="M4 15v3a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-3"/>',
    "Package": '<path d="M4 8l8-4 8 4-8 4z"/><path d="M4 8v8l8 4 8-4V8"/><path d="M12 12v8"/>',
    "GameCard": '<path d="M6 3.5h9l3 3V20a.5.5 0 0 1-.5.5h-11A.5.5 0 0 1 6 20z"/><path d="M9 8h6v5H9z"/><path d="M9 16.5h6"/>',
    # Not one of the app's icons: a sparkle in the same 24-grid, 2 px round
    # stroke style, for "Little things".
    "Sparkle": '<path d="M11 3.5l1.9 5.1 5.1 1.9-5.1 1.9L11 17.5l-1.9-5.1L4 10.5l5.1-1.9z"/><path d="M18 14.5l.9 2.1 2.1.9-2.1.9-.9 2.1-.9-2.1-2.1-.9 2.1-.9z"/>',
    "Heart": '<path d="M12 19.2s-7.2-4.3-7.2-9.6a3.9 3.9 0 0 1 7.2-2.1 3.9 3.9 0 0 1 7.2 2.1c0 5.3-7.2 9.6-7.2 9.6z"/>',
    "Chip": '<rect x="6" y="6" width="12" height="12" rx="1.5"/><path d="M9 3v3M15 3v3M9 18v3M15 18v3M3 9h3M3 15h3M18 9h3M18 15h3"/>',
}
C = dict(blue="#1C93C2", teal="#1E9E84", red="#D2603A", orange="#E9890E", grey="#7D7563", green="#32A865", pink="#C05BA8", gold="#C9A227")

def icon(name, color, size, extra=""):
    return (f'<svg width="{size}" height="{size}" viewBox="0 0 24 24" fill="none" stroke="{color}" '
            f'stroke-width="2" stroke-linecap="round" stroke-linejoin="round" {extra}>{ICONS[name]}</svg>')

def words(text, cls="w"):
    return " ".join(f'<span class="mask"><span class="{cls}">{w}</span></span>' for w in text.split())

FONTS = """
        @font-face { font-family: "M PLUS Rounded 1c"; font-weight: 500; src: url("assets/fonts/m-plus-rounded-1c-500-latin.woff2") format("woff2"); }
        @font-face { font-family: "M PLUS Rounded 1c"; font-weight: 700; src: url("assets/fonts/m-plus-rounded-1c-700-latin.woff2") format("woff2"); }
        @font-face { font-family: "M PLUS Rounded 1c"; font-weight: 800; src: url("assets/fonts/m-plus-rounded-1c-800-latin.woff2") format("woff2"); }
        @font-face { font-family: "Gaegu"; font-weight: 700; src: url("assets/fonts/gaegu-700-latin.woff2") format("woff2"); }
        @font-face { font-family: "M PLUS 1 Code"; font-weight: 500; src: url("assets/fonts/m-plus-1-code-500-latin.woff2") format("woff2"); }"""

def scene(num, name, dur, note, css, html, js):
    cid = f"s{num:02d}-{name}"
    out = f"""<!doctype html>
<html>
  <head>
    <meta charset="UTF-8" />
    <!-- {note} -->
  </head>
  <body>
    <template>
      <link rel="stylesheet" href="assets/css/system.css" />
      <style>{FONTS}
        #root {{
          position: absolute;
          inset: 0;
          overflow: hidden;
        }}
        .clip {{
          position: absolute;
          inset: 0;
        }}
{css}
      </style>

      <div id="root" data-composition-id="{cid}" data-width="1920" data-height="1080">
        <div id="{cid}-stage" class="clip" data-layout-allow-overflow>
{html}
        </div>
      </div>

      <script>
        const tl = gsap.timeline({{ paused: true }});
{js}
        window.__timelines["{cid}"] = tl;
      </script>
    </template>
  </body>
</html>
"""
    path = os.path.join(FR, f"{num:02d}-{name}.html")
    open(path, "w").write(out)
    return cid, f"compositions/frames/{num:02d}-{name}.html"

def card(cid, x, y, w, h, clip=None, which="top", mstart=0, start=0, dur=4, poster=None, extra_cls=""):
    """A floating screen card holding a timed, muted clip (and a poster under it)."""
    inner = ""
    if poster:
        inner += f'<img src="{poster}" alt="" />'
    if clip:
        inner += (f'<video id="{cid}-v" src="assets/footage/{clip}-{which}.mp4" muted playsinline '
                  f'data-start="{start}" data-duration="{dur}" data-media-start="{mstart}" data-track-index="1"></video>')
    return f'<div id="{cid}" class="screen-card {extra_cls}" style="left:{x}px;top:{y}px;width:{w}px;height:{h}px">{inner}</div>'

ENTER_CARDS = """        tl.fromTo({sel}, {{ y: 70, scale: 0.96, opacity: 0, filter: "blur(8px)" }},
          {{ y: 0, scale: 1, opacity: 1, filter: "blur(0px)", duration: 0.85, ease: "expo.out", stagger: 0.1 }}, {at});
        tl.to({sel}, {{ y: "-=10", duration: 1.8, ease: "sine.inOut", yoyo: true, repeat: 1, stagger: 0.3 }}, {float_at});"""
EXIT = """        tl.to({sel}, {{ yPercent: -8, opacity: 0, filter: "blur(6px)", duration: 0.32, ease: "power2.in", stagger: 0.025 }}, {at});"""

scenes = []  # (cid, src, start, dur)

STEP_CSS = """
        .st { position: absolute; width: 640px; height: 150px; }
        .st-item { position: absolute; left: 0; top: 0; display: flex; align-items: flex-start; gap: 22px; }
        .st-num { flex: none; width: 60px; height: 60px; margin-top: 4px; border-radius: 50%; background: #17799f; color: #fff;
          display: flex; align-items: center; justify-content: center; font-family: "M PLUS Rounded 1c", sans-serif; font-weight: 800; font-size: 32px; }
        .st-title { font-family: "M PLUS Rounded 1c", sans-serif; font-weight: 800; font-size: 54px; line-height: 1.1; color: #615a4b; white-space: nowrap; }
        .st-sub { margin-top: 6px; font-family: "M PLUS Rounded 1c", sans-serif; font-weight: 700; font-size: 34px; line-height: 1.2; color: #756e60; white-space: nowrap; }
        .st-dots { position: absolute; left: 82px; top: 128px; display: flex; gap: 10px; }
        .st-dot { width: 12px; height: 12px; border-radius: 50%; background: #d0c78e; }
        .chip { position: absolute; display: flex; align-items: center; gap: 10px; padding: 10px 22px; border-radius: 999px; background: #fff;
          box-shadow: 0 14px 30px rgba(70, 60, 20, 0.25); font-family: "M PLUS Rounded 1c", sans-serif; font-weight: 700; font-size: 30px;
          color: #615a4b; white-space: nowrap; }
"""

def steps(prefix, x, y, items):
    """Captions that take turns: items = [(title, sub), ...]."""
    out = f'<div class="st" id="{prefix}" style="left:{x}px;top:{y}px">'
    for i, (title, sub) in enumerate(items):
        out += (f'<div class="st-item" id="{prefix}-{i}" data-layout-allow-overlap>'
                f'<div><div class="st-title">{words(title)}</div><div class="st-sub">{words(sub)}</div></div></div>')
    return out + "</div>"

def steps_js(prefix, times, end):
    """times[i] = when caption i arrives; it leaves just before the next one (or `end`)."""
    js = f'        tl.set("#{prefix} .st-item", {{ opacity: 0 }}, 0);\n'
    for i, t in enumerate(times):
        nxt = times[i + 1] if i + 1 < len(times) else end
        js += f'        tl.set("#{prefix}-{i}", {{ opacity: 1 }}, {t});\n'
        js += f'        tl.fromTo("#{prefix}-{i} .w", {{ opacity: 0, y: 26, filter: "blur(10px)" }}, {{ opacity: 1, y: 0, filter: "blur(0px)", duration: 0.85, ease: "expo.out", stagger: 0.05, immediateRender: false }}, {t + 0.05});\n'
        if i + 1 < len(times):
            js += f'        tl.to("#{prefix}-{i} .w", {{ opacity: 0, y: -14, filter: "blur(8px)", duration: 0.3, ease: "power2.in", stagger: 0.015, immediateRender: false }}, {nxt - 0.28});\n'
            js += f'        tl.set("#{prefix}-{i}", {{ opacity: 0 }}, {nxt});\n'
    return js

def seg_video(vid, clip, which, mstart, start, dur, track=1, ext="mp4", rate=None):
    r = f' data-playback-rate="{rate}"' if rate else ""
    return (f'<video id="{vid}" class="seg" src="assets/footage/{clip}-{which}.{ext}" muted playsinline '
            f'data-start="{start}" data-duration="{dur}" data-media-start="{mstart}"{r} data-track-index="{track}"></video>')

def chip_js(sel, x0, y0, x1, y1, at):
    return (f'        tl.fromTo("{sel}", {{ x: {x0}, y: {y0}, scale: 0.4, opacity: 0 }}, {{ scale: 1, opacity: 1, duration: 0.2, ease: "back.out(2)" }}, {at});\n'
            f'        tl.to("{sel}", {{ x: {x1}, y: {y1}, duration: 0.5, ease: "power2.inOut" }}, {at + 0.15});\n'
            f'        tl.to("{sel}", {{ scale: 0.3, opacity: 0, duration: 0.15, ease: "power1.in" }}, {at + 0.62});\n')

# ================================================================ 01 intro
# The 3D console (tools/console-3d, rendered to assets/footage/console-open.webm
# with alpha) wakes up on the right while three short lines play on the left.
css = """
        #i-cwrap { position: absolute; left: 0; top: 0; width: 1920px; height: 1080px; transform-origin: 1720px 600px; }
        #i-console { position: absolute; inset: 0; width: 1920px; height: 1080px; }
        .i-line { position: absolute; left: 120px; top: 300px; width: 700px; }
        .i-line .t-story { font-size: 70px; white-space: nowrap; }
        #i-l2, #i-l3 { opacity: 0; }
        #i-fbi { position: absolute; left: 120px; top: 500px; width: 300px; height: 100px; }
        #i-fbi .row { position: absolute; inset: 0; justify-content: center; gap: 18px; font-size: 48px; font-weight: 800; }
"""
html = f"""
          <div id="i-cwrap"><video id="i-console" src="assets/footage/console-open.webm" muted playsinline data-start="0" data-duration="8" data-media-start="0" data-track-index="1"></video></div>
          <div id="i-l1" class="i-line" data-layout-allow-overlap><div class="t-story">{words("For years, FBI was")}</div><div class="t-story">{words("how homebrew got")}</div><div class="t-story">{words("onto your 3DS.")}</div></div>
          <div id="i-l2" class="i-line" data-layout-allow-overlap><div class="t-story">{words("Thank you,")}</div><div class="t-story blue">{words("Steveice10.")}</div></div>
          <div id="i-l3" class="i-line" data-layout-allow-overlap><div class="t-story">{words("Here's what")}</div><div class="t-story blue">{words("comes next.")}</div></div>"""
js = """
        // Keep the console clear of the words: smaller and further right while
        // the first two lines are up, back to its rendered size before the push.
        tl.fromTo("#i-cwrap", { x: 110, scale: 0.88 }, { x: 110, scale: 0.88, duration: 4.4, ease: "none" }, 0);
        tl.to("#i-cwrap", { x: 0, scale: 1, duration: 1.2, ease: "power2.inOut" }, 4.4);
        tl.fromTo("#i-l1 .w", { opacity: 0, y: 26, filter: "blur(10px)" }, { opacity: 1, y: 0, filter: "blur(0px)", duration: 0.85, ease: "expo.out", stagger: 0.05 }, 0.45);
        tl.to("#i-l1 .w", { opacity: 0, y: -14, filter: "blur(8px)", duration: 0.34, ease: "power2.in", stagger: 0.015 }, 2.45);
        tl.set("#i-l1", { opacity: 0 }, 2.95);
        tl.set("#i-l2", { opacity: 1 }, 2.7);
        tl.fromTo("#i-l2 .w", { opacity: 0, y: 26, filter: "blur(10px)" }, { opacity: 1, y: 0, filter: "blur(0px)", duration: 0.85, ease: "expo.out", stagger: 0.05 }, 2.75);
        tl.to("#i-l2 .w", { opacity: 0, y: -14, filter: "blur(8px)", duration: 0.34, ease: "power2.in", stagger: 0.015 }, 4.05);
        tl.set("#i-l2", { opacity: 0 }, 4.5);
        tl.set("#i-l3", { opacity: 1 }, 4.3);
        tl.fromTo("#i-l3 .w", { opacity: 0, y: 26, filter: "blur(10px)" }, { opacity: 1, y: 0, filter: "blur(0px)", duration: 0.85, ease: "expo.out", stagger: 0.05 }, 4.35);
        tl.to("#i-l3 .w", { opacity: 0, y: -14, filter: "blur(8px)", duration: 0.34, ease: "power2.in", stagger: 0.015 }, 5.75);"""
scenes.append((*scene(1, "intro", 8, "Opening: the 3D console wakes, opens and boots; the camera pushes into its top screen.", css, html, js), 0, 8))

# ================================================================ 03 title
tiles = [("Scan", C["blue"], 480, 170), ("Globe", C["teal"], 320, 440), ("Chip", C["pink"], 540, 800),
         ("History", C["red"], 1300, 170), ("Folder", C["orange"], 1460, 440), ("GameCard", C["gold"], 1240, 800)]
css = """
        #t-icon { position: absolute; left: 840px; top: 210px; width: 240px; height: 240px; border-radius: 54px;
          box-shadow: 0 30px 60px rgba(70, 60, 20, 0.24); }
        #t-word { position: absolute; left: 0; right: 0; top: 480px; display: flex; justify-content: center; }
        #t-word span { display: block; }
        #t-tag { position: absolute; left: 0; right: 0; top: 690px; text-align: center; }
        .t-tile { position: absolute; width: 140px; height: 140px; border-radius: 32px; display: flex;
          align-items: center; justify-content: center;
          background: linear-gradient(180deg, #fefcf5 0%, #e0dccb 85%, #b6b192 100%);
          border-bottom: 3px solid #8f8659; box-shadow: 0 18px 30px rgba(70, 60, 20, 0.18); }
"""
th = "\n".join(f'          <div class="t-tile" style="left:{x}px;top:{y}px">{icon(n, c, 84)}</div>' for n, c, x, y in tiles)
html = f"""
{th}
          <img id="t-icon" src="assets/brand/icon-1024.png" alt="" />
          <div id="t-word" class="t-hero"><span>3</span><span>d</span><span>s</span><span class="blue">D</span><span class="blue">L</span></div>
          <div id="t-tag" class="t-line">{words("Download anything, straight to your 3DS.")}</div>"""
js = """
        tl.fromTo("#t-icon", { scale: 0, rotation: -14 }, { scale: 1, rotation: 0, duration: 0.75, ease: "back.out(2.4)" }, 0);
        tl.fromTo(".t-tile", { x: (i) => [380, 540, 320, -340, -500, -280][i], y: (i) => [160, -110, -470, 160, -110, -470][i], scale: 0.2, opacity: 0 },
          { x: 0, y: 0, scale: 1, opacity: 1, duration: 0.9, ease: "back.out(1.5)", stagger: 0.05 }, 0.1);
        tl.to(".t-tile", { y: "-=14", duration: 1.4, ease: "sine.inOut", yoyo: true, repeat: 1, stagger: 0.12 }, 1.0);
        tl.fromTo("#t-word span", { y: 80, opacity: 0 }, { y: 0, opacity: 1, duration: 0.5, ease: "back.out(2)", stagger: 0.07 }, 0.3);
        tl.fromTo("#t-tag .w", { opacity: 0, y: 26, filter: "blur(10px)" }, { opacity: 1, y: 0, filter: "blur(0px)", duration: 0.85, ease: "expo.out", stagger: 0.05 }, 1.0);
        tl.to(".t-tile", { scale: 0.6, opacity: 0, duration: 0.4, ease: "power3.in", stagger: 0.03 }, 3.45);
        tl.to("#t-word, #t-tag", { y: -30, opacity: 0, filter: "blur(8px)", duration: 0.36, ease: "power2.in" }, 3.55);
        tl.to("#t-icon", { scale: 0.5, opacity: 0, duration: 0.35, ease: "power3.in" }, 3.6);"""
scenes.append((*scene(3, "title", 4, "The drop: icon into the ring, wordmark, Home's six tiles fan out.", css, html, js), 8, 4))

# ================================================================ 04 home
K = 1100 / 1356
css = f"""
        #h-copy {{ position: absolute; left: 110px; top: 340px; width: 680px; }}
        #h-copy .t-story {{ font-size: 66px; white-space: nowrap; }}
        #h-copy .t-line {{ margin-top: 28px; font-size: 40px; width: 560px; }}
        #h-device {{ position: absolute; left: 700px; top: 70px; width: 1100px; height: 955px; }}
        #h-photo {{ position: absolute; inset: 0; width: 1100px; height: 955px;
          filter: drop-shadow(0 34px 40px rgba(60, 50, 20, 0.3)); }}
        .h-scr {{ position: absolute; left: 0; top: 0; overflow: hidden; transform-origin: 0 0; background: #000; }}
        .h-scr video {{ position: absolute; inset: 0; width: 100%; height: 100%; }}
        .h-scr::after {{ content: ""; position: absolute; inset: 0;
          background: linear-gradient(105deg, rgba(255, 255, 255, 0.14) 0%, rgba(255, 255, 255, 0) 45%); }}
        #h-top {{ width: 400px; height: 240px;
          transform: scale({K:.6f}) matrix3d(1.025661, 0.317498, 0, -0.000237, -0.41727, 1.468451, 0, 0.000048, 0, 0, 1, 0, 611.231497, 133.366913, 0, 1); }}
        #h-bottom {{ width: 320px; height: 240px;
          transform: scale({K:.6f}) matrix3d(1.063699, 0.348492, 0, -0.000208, -1.145845, 0.47126, 0, -0.000304, 0, 0, 1, 0, 493.838504, 590.464957, 0, 1); }}
"""
html = f"""
          <div id="h-copy">
            <div class="t-story">{words("Feels like it came")}</div>
            <div class="t-story">{words("with the console.")}</div>
            <div class="t-line">{words("Designed like the 3DS's own system apps.")}</div>
          </div>
          <div id="h-device">
            <img id="h-photo" src="assets/device/new-3ds-angled.png" alt="" />
            <div id="h-top" class="h-scr"><video id="h-top-v" src="assets/footage/home-top.mp4" muted playsinline data-start="0" data-duration="4" data-media-start="0" data-track-index="1"></video></div>
            <div id="h-bottom" class="h-scr"><video id="h-bottom-v" src="assets/footage/home-bottom.mp4" muted playsinline data-start="0" data-duration="4" data-media-start="0" data-track-index="2"></video></div>
          </div>"""
js = """
        tl.fromTo("#h-device", { x: 620, y: 70, rotation: 7, scale: 0.9, opacity: 0 },
          { x: 0, y: 0, rotation: 0, scale: 1, opacity: 1, duration: 0.95, ease: "back.out(1.2)" }, 0);
        tl.fromTo("#h-copy .w", { opacity: 0, y: 26, filter: "blur(10px)" }, { opacity: 1, y: 0, filter: "blur(0px)", duration: 0.85, ease: "expo.out", stagger: 0.05 }, 0.35);
        tl.to("#h-copy .w", { opacity: 0, y: -14, filter: "blur(8px)", duration: 0.34, ease: "power2.in", stagger: 0.015 }, 2.95);
        tl.to("#h-device", { x: 560, scale: 0.92, opacity: 0, duration: 0.6, ease: "power3.in" }, 3.1);"""
scenes.append((*scene(4, "home", 4, "The real console, Home playing on its screens; the ring lands on Scan QR Code.", css, html, js), 12, 4))

# ---------------------------------------------------------------- chapter scenes (right side)
def chapter(num, name, dur, note, cards_html, cards_sel, extra_css="", extra_html="", extra_js="", exit_sel=None):
    css = extra_css
    js = ENTER_CARDS.format(sel=json.dumps(cards_sel), at=0.03, float_at=1.0) + "\n" + extra_js + "\n" + \
        EXIT.format(sel=json.dumps(exit_sel or cards_sel), at=round(dur - 0.34, 2))
    return scene(num, name, dur, note, css, cards_html + extra_html, js)

# 05 scan (8 s, qrScan from 0.8)
cards = "\n          " + card("sc-top", 760, 96, 1000, 600, "qrScan", "top", 0.8, 0, 8) + \
        "\n          " + card("sc-bot", 1330, 650, 520, 390, "qrScan", "bottom", 0.8, 0, 8)
dood = """
          <div id="sc-doodle" class="doodle" style="left:1080px;top:22px"><div id="sc-dt">4 connections at once!</div></div>
          <svg id="sc-arrow" class="doodle" style="left:1500px;top:70px" width="220" height="280" viewBox="0 0 220 280"><path id="sc-a1" d="M60 6 C 130 40, 150 140, 120 236"/><path id="sc-a2" d="M96 214 L 120 238 L 140 210"/></svg>"""
js = """
        tl.fromTo("#sc-dt", { opacity: 0, rotation: -7, y: 12 }, { opacity: 1, rotation: -3, y: 0, duration: 0.35, ease: "back.out(2)" }, 6.25);
        tl.fromTo("#sc-a1", { strokeDasharray: 300, strokeDashoffset: 300 }, { strokeDashoffset: 0, duration: 0.45, ease: "power2.out" }, 6.4);
        tl.fromTo("#sc-a2", { strokeDasharray: 80, strokeDashoffset: 80 }, { strokeDashoffset: 0, duration: 0.2, ease: "power2.out" }, 6.8);
        tl.to("#sc-doodle, #sc-arrow", { opacity: 0, duration: 0.3 }, 7.55);"""
scenes.append((*chapter(5, "scan", 8, "Scan: the camera finds a QR code, Download, four connections.", cards, ".screen-card",
                        extra_html=dood, extra_js=js), 16, 8))

# 06 faster
lanes = [("FBI", "through the system HTTP service", 0.19, "~110", "KiB/s", False),
         ("3dsDL", "one connection", 0.53, "~315", "KiB/s", False),
         ("3dsDL", "four connections at once", 0.93, "500–600", "KiB/s", True)]
lane_html = ""
for i, (n, sub, p, val, unit, four) in enumerate(lanes):
    y = 360 + i * 165
    fill = ("".join(f'<div class="f-strand" style="top:{j*8}px"></div>' for j in range(4)) if four
            else '<div class="f-fill"></div>')
    lane_html += f"""
          <div class="f-lane" id="f-lane{i}" style="top:{y}px">
            <div class="f-name"><span class="{'blue' if n == '3dsDL' else 'fbi'}">{n}</span> <span class="f-sub">{sub}</span></div>
            <div class="f-track"><div class="f-bar" id="f-bar{i}" data-p="{p}">{fill}</div></div>
            <div class="f-read" id="f-read{i}"><b>{val}</b> <span>{unit}</span></div>
          </div>"""
css = """
        #f-head { position: absolute; left: 760px; top: 96px; width: 1100px; font-size: 84px; }
        .f-lane { position: absolute; left: 760px; width: 1080px; height: 150px; }
        .f-name { font-family: "M PLUS Rounded 1c", sans-serif; font-weight: 800; font-size: 46px; color: #615a4b; }
        .f-name .fbi { color: #756e60; }
        .f-sub { font-weight: 700; font-size: 34px; color: #8c8466; }
        .f-track { position: absolute; left: 0; top: 76px; width: 760px; height: 32px; border-radius: 16px; background: #cfc795; overflow: hidden; }
        .f-bar { position: absolute; left: 0; top: 0; height: 32px; width: 760px; transform-origin: 0 50%; }
        .f-fill { position: absolute; inset: 0; border-radius: 16px; background: linear-gradient(90deg, #ffcc22, #ff9900); }
        .f-strand { position: absolute; left: 0; right: 0; height: 6px; border-radius: 3px; background: linear-gradient(90deg, #ffcc22, #ff9900); }
        .f-read { position: absolute; left: 790px; top: 56px; width: 290px; font-family: "M PLUS Rounded 1c", sans-serif; color: #615a4b; white-space: nowrap; }
        .f-read b { font-weight: 800; font-size: 60px; }
        .f-read span { font-weight: 700; font-size: 30px; color: #8c8466; }
        #f-lane2 .f-read b { color: #17799f; }
        #f-note { position: absolute; left: 760px; top: 900px; width: 1080px; }
"""
html = f"""
          <div id="f-head" class="t-chapter">{words("Four connections")} <span class="blue">{words("at once.")}</span></div>
{lane_html}
          <div id="f-note" class="t-small">Download speeds measured on a New 3DS: FBI 2.6.1 and a single libcurl connection from the figures in FBI pull request #4, 3dsDL on four connections. Not same-file runs; your speed depends on the server and your Wi-Fi.</div>"""
js = """
        tl.fromTo("#f-head .w", { opacity: 0, y: 26, filter: "blur(10px)" }, { opacity: 1, y: 0, filter: "blur(0px)", duration: 0.85, ease: "expo.out", stagger: 0.05 }, 0.15);
        tl.fromTo(".f-lane", { y: 30, opacity: 0, filter: "blur(8px)" }, { y: 0, opacity: 1, filter: "blur(0px)", duration: 0.8, ease: "expo.out", stagger: 0.1 }, 0.3);
        tl.fromTo("#f-note", { opacity: 0 }, { opacity: 1, duration: 0.5 }, 0.8);
        [0.19, 0.53, 0.93].forEach((p, i) => {
          tl.fromTo("#f-bar" + i, { scaleX: 0 }, { scaleX: p, duration: 1.0 + p * 1.4, ease: "power2.out" }, 0.75);
          tl.fromTo("#f-read" + i, { opacity: 0, scale: 0.6 }, { opacity: 1, scale: 1, duration: 0.35, ease: "back.out(2.4)" }, 0.75 + 0.8 + p * 1.1);
        });
        tl.fromTo(".f-strand", { x: (i) => -30 * i }, { x: 0, duration: 2.2, ease: "power2.out" }, 0.75);
        tl.to("#f-head, .f-lane, #f-note", { yPercent: -8, opacity: 0, filter: "blur(6px)", duration: 0.32, ease: "power2.in", stagger: 0.025 }, 3.66);"""
scenes.append((*scene(6, "faster", 4, "Speed: three of the app's progress bars, measured figures, footnote.", css, html, js), 24, 4))

# 07 install (bottom screen big)
cards = "\n          " + card("in-bot", 740, 100, 720, 540, "install", "bottom", 1.2, 0, 4) + \
        "\n          " + card("in-top", 1240, 610, 600, 360, "install", "top", 1.2, 0, 4)
scenes.append((*chapter(7, "install", 4, "Install: prompt, progress, Installed, Launch.", cards, ".screen-card"), 28, 4))

# 08 qol
qol = [("qolQueue", "Download Queue", 720, 250), ("qolResume", "Resume failed downloads", 1100, 250), ("moreUpdates", "Updates itself", 1480, 250),
       ("moreClock", "Fixes the clock", 720, 620), ("qolZip", "Extract ZIPs", 1100, 620), ("qolHttps", "Verified HTTPS", 1480, 620)]
cards = ""
for i, (clip, tag, x, y) in enumerate(qol):
    st = round(0.3 + 0.45 * i, 2)
    d = round(min(3.0, 3.95 - st), 2)
    cards += f"""
          <div class="q-cell" id="q-cell{i}" style="left:{x}px;top:{y}px">
            {card(f"q-c{i}", 0, 0, 370, 222, clip, "top", 0, st, d, poster=f"assets/footage/posters/{clip}-top-last.jpg")}
            <div class="tag q-tag">{tag}</div>
          </div>"""
css = """
        .q-cell { position: absolute; width: 370px; height: 280px; }
        .q-cell .screen-card { border-width: 6px; border-radius: 20px; }
        .q-tag { position: absolute; left: 14px; top: 200px; font-size: 26px; box-shadow: 0 8px 16px rgba(70, 60, 20, 0.2); }
"""
js = """
        tl.fromTo(".q-cell", { y: 70, scale: 0.85, opacity: 0, rotation: (i) => (i % 2 ? 3 : -3) },
          { y: 0, scale: 1, opacity: 1, rotation: 0, duration: 0.55, ease: "back.out(1.7)", stagger: 0.45 }, 0.3);
        tl.fromTo(".q-tag", { scale: 0, opacity: 0 }, { scale: 1, opacity: 1, duration: 0.3, ease: "back.out(2.4)", stagger: 0.45 }, 0.5);
        tl.to(".q-cell", { yPercent: -8, opacity: 0, filter: "blur(6px)", duration: 0.32, ease: "power2.in", stagger: 0.025 }, 3.66);"""
scenes.append((*scene(8, "qol", 4, "The little things: six moving screens on the beat, a tag each.", css, cards, js), 32, 4))

# 09 files: four steps in order. Segments of the files clip, recut:
# browse (0-1.7 s), the notes.txt preview (a held frame at 5.5 s), editing
# it (5.75-8.3 s), then the picture viewer (2.9-4.65 s).
FSEG = [(0.0, 0.2, 1.7), (None, 1.9, 1.4), (5.75, 3.3, 2.55), (2.9, 5.85, 2.15)]
def files_card(cid, which, x, y, w, h):
    inner = ""
    for k, (ms, st, d) in enumerate(FSEG):
        if ms is None:
            inner += f'<img class="clip seg" id="{cid}-s{k}" src="assets/footage/stills/files-textpreview-{which}.png" alt="" data-start="{st}" data-duration="{d}" data-track-index="2" />'
        else:
            inner += seg_video(f"{cid}-s{k}", "files", which, ms, st, d, track=1 + (k % 2))
    return f'<div id="{cid}" class="screen-card" style="left:{x}px;top:{y}px;width:{w}px;height:{h}px">{inner}</div>'
cards = ("\n          " + files_card("fi-top", "top", 760, 96, 1000, 600) +
         "\n          " + files_card("fi-bot", "bottom", 1330, 650, 520, 390))
css = STEP_CSS + """
        .screen-card .seg { position: absolute; inset: 0; width: 100%; height: 100%; object-fit: cover; }
"""
js = ENTER_CARDS.format(sel=json.dumps(".screen-card"), at=0.03, float_at=1.0) + "\n" + \
     EXIT.format(sel=json.dumps(".screen-card"), at=7.66)
scenes.append((*scene(9, "files", 8, "Files, step by step: browse, peek inside a text file, edit it, view pictures.", css, cards, js), 36, 8))

# 10 titles
cards = "\n          " + card("ti-top", 720, 400, 960, 576, "titles", "top", 7.6, 0, 4) + \
        "\n          " + card("ti-bot", 1300, 70, 520, 390, "titles", "bottom", 7.6, 0, 4)
scenes.append((*chapter(10, "titles", 4, "Titles and saves: a backup in Checkpoint's format.", cards, ".screen-card"), 44, 4))

# 11 receive
css = """
        #re-browser { position: absolute; left: 740px; top: 36px; width: 1136px; height: 710px;
          filter: drop-shadow(0 34px 50px rgba(70, 60, 20, 0.28)); }
        #re-browser video { position: absolute; inset: 0; width: 100%; height: 100%; }
        #re-pill { position: absolute; left: 1180px; top: 330px; display: flex; align-items: center; gap: 12px;
          padding: 12px 24px; border-radius: 999px; background: #fff; box-shadow: 0 14px 30px rgba(70, 60, 20, 0.25);
          font-family: "M PLUS Rounded 1c", sans-serif; font-weight: 700; font-size: 32px; color: #615a4b; white-space: nowrap; }
"""
html = f"""
          <div id="re-browser"><video id="re-browser-v" src="assets/footage/browser-receive.webm" muted playsinline data-start="0" data-duration="4" data-media-start="4" data-track-index="1"></video></div>
          {card("re-top", 690, 700, 520, 312, "receive", "top", 4.0, 0, 4)}
          {card("re-bot", 1260, 720, 400, 300, "receive", "bottom", 4.0, 0, 4)}
          <div id="re-pill">{icon("Download", C["blue"], 34)}<span>Checkpoint.cia</span></div>"""
js = """
        tl.fromTo("#re-browser", { y: 70, scale: 0.96, opacity: 0 }, { y: 0, scale: 1, opacity: 1, duration: 0.85, ease: "expo.out" }, 0.03);
        tl.fromTo("#re-top, #re-bot", { y: 80, opacity: 0, filter: "blur(8px)" }, { y: 0, opacity: 1, filter: "blur(0px)", duration: 0.85, ease: "expo.out", stagger: 0.1 }, 0.12);
        tl.to("#re-top, #re-bot, #re-browser", { y: "-=10", duration: 1.2, ease: "sine.inOut", yoyo: true, repeat: 1, stagger: 0.2 }, 1.1);
        tl.fromTo("#re-pill", { x: 0, y: 0, scale: 0.4, opacity: 0 }, { scale: 1, opacity: 1, duration: 0.2, ease: "back.out(2)" }, 1.55);
        tl.to("#re-pill", { x: 160, y: 470, duration: 0.45, ease: "power2.in" }, 1.7);
        tl.to("#re-pill", { scale: 0.3, opacity: 0, duration: 0.15, ease: "power1.in" }, 2.05);
        tl.to("#re-browser, #re-top, #re-bot", { yPercent: -8, opacity: 0, filter: "blur(6px)", duration: 0.32, ease: "power2.in", stagger: 0.025 }, 3.66);"""
scenes.append((*scene(11, "receive", 4, "Receive: paste a link on the page, the 3DS downloads it. The part FBI users know.", css, html, js), 48, 4))

# 12 wifi: Files over Wi-Fi, the new part, explained in four steps.
# Browser and console on one clock (same media start for both).
# One continuous take, no cuts: from the click on the Files tab (9.65 s in
# the recording) through the drop, the uploads, the CIA install, browsing
# and the download (19.2 s), at 1.06x so it fits the 10 s chapter.
WM, WR = 9.2, 1.06
wt = lambda media: round((media - WM) / WR, 2)  # recording time -> scene time
bro = seg_video("wf-b", "browser", "receive", WM, 0, 10, ext="webm", rate=WR)
con = seg_video("wf-c", "receive", "bottom", WM, 0, 10, track=3, rate=WR)
css = STEP_CSS + """
        #wf-browser { position: absolute; left: 730px; top: 96px; width: 1100px; height: 688px;
          filter: drop-shadow(0 34px 50px rgba(70, 60, 20, 0.28)); }
        #wf-browser video { position: absolute; inset: 0; width: 100%; height: 100%; }
        #wf-url { position: absolute; left: 890px; top: 300px; width: 640px; height: 100px; border-radius: 50px; background: #fff;
          display: flex; align-items: center; gap: 22px; padding: 0 36px; box-shadow: 0 24px 50px rgba(70, 60, 20, 0.28), 0 0 0 3px #a8d8e8; }
        #wf-url-t { font-family: "M PLUS Rounded 1c", sans-serif; font-weight: 800; font-size: 52px; color: #615a4b; }
        #wf-caret { width: 4px; height: 54px; background: #17799f; border-radius: 2px; }
        #wf-con .seg { position: absolute; inset: 0; width: 100%; height: 100%; object-fit: cover; }
"""
html = f"""
          <div id="wf-browser">{bro}</div>
          <div id="wf-con" class="screen-card" style="left:1430px;top:700px;width:420px;height:315px">{con}</div>
          <div id="wf-url">{icon("Globe", C["teal"], 46)}<span id="wf-url-t">{"".join(f'<span class="wf-ch">{c}</span>' for c in "3ds.local")}</span><span id="wf-caret"></span></div>
          <div class="chip" id="wf-chip1">{icon("Folder", C["orange"], 32)}<span>theme.zip</span></div>
          <div class="chip" id="wf-chip2" style="background:#3fa65a;color:#fff">{icon("Package", "#ffffff", 32)}<span>Install request</span></div>
          <div class="chip" id="wf-chip3">{icon("Download", C["blue"], 32)}<span>top_0001.bmp</span></div>"""
js = """
        tl.fromTo("#wf-browser", { y: 70, scale: 0.96, opacity: 0 }, { y: 0, scale: 1, opacity: 1, duration: 0.85, ease: "expo.out" }, 0.03);
        tl.fromTo("#wf-con", { y: 80, opacity: 0, filter: "blur(8px)" }, { y: 0, opacity: 1, filter: "blur(0px)", duration: 0.85, ease: "expo.out" }, 0.15);
        tl.set(".chip", { left: 0, top: 0, opacity: 0 }, 0);
        // A big address bar types 3ds.local, then shrinks into the browser's own.
        tl.fromTo("#wf-url", { scale: 0.9, opacity: 0, y: 24, filter: "blur(8px)" }, { scale: 1, opacity: 1, y: 0, filter: "blur(0px)", duration: 0.6, ease: "expo.out" }, 0.15);
        tl.set(".wf-ch", { opacity: 0 }, 0);
        gsap.utils.toArray(".wf-ch").forEach((el, i) => tl.set(el, { opacity: 1 }, 0.35 + i * 0.07));
        tl.to("#wf-caret", { opacity: 0, duration: 0.01, yoyo: true, repeat: 5, repeatDelay: 0.2 }, 0.2);
        tl.to("#wf-url", { x: -300, y: -230, scale: 0.35, opacity: 0, filter: "blur(4px)", duration: 0.5, ease: "power3.inOut" }, 1.25);
""" + \
    chip_js("#wf-chip1", 1180, 430, 1530, 800, wt(11.0)) + chip_js("#wf-chip2", 1240, 470, 1510, 800, wt(14.0)) + chip_js("#wf-chip3", 1510, 810, 1250, 370, wt(18.2)) + """
        tl.to("#wf-browser, #wf-con", { yPercent: -8, opacity: 0, filter: "blur(6px)", duration: 0.32, ease: "power2.in", stagger: 0.025 }, 9.66);"""
scenes.append((*scene(12, "wifi", 10, "Files over Wi-Fi, explained: open 3ds.local, drop files in, send a CIA, browse and download.", css, html, js), 52, 10))

# 12 more
# (clip, tag, media start, longest it plays, poster under it)
more = [("moreHistory", "History", 0, 3.0, "moreHistory-top-last"), ("titles", "Tickets & seeds", 5.3, 1.4, "titles-tickets-top"),
        ("moreHex", "Hex view", 0, 3.0, "moreHex-top-last"), ("moreSha", "SHA-256 checks", 0, 3.0, "moreSha-top-last"),
        ("moreSettings", "Settings", 0, 3.0, "moreSettings-top-last"), ("moreReport", "Report an issue", 0, 3.0, "moreReport-top-last")]
# A 3 x 2 grid that fills the right two-thirds, the middle column dropped a
# little for rhythm; every card carries its label, as in "Little things".
CW, CH_, GX, GY = 360, 216, 34, 96
pos = [(c * (CW + GX), r * (CH_ + GY) + (44 if c == 1 else 0)) for r in range(2) for c in range(3)]
cards = ""
for i, ((clip, tag, ms, cap, poster), (x, y)) in enumerate(zip(more, pos)):
    st = round(0.25 + 0.07 * i, 2)
    cards += f"""
            <div class="mo-cell" id="mo-cell{i}" style="left:{x}px;top:{y}px">
              {card(f"mo-c{i}", 0, 0, CW, CH_, clip, "top", ms, st, round(min(cap, 3.95 - st), 2), poster=f"assets/footage/posters/{poster}.jpg", extra_cls="mo-card")}
              <div class="tag mo-tag">{tag}</div>
            </div>"""
css = """
        #mo-head { position: absolute; left: 110px; top: 350px; width: 580px; font-size: 112px; }
        #mo-sub { position: absolute; left: 114px; top: 625px; width: 500px; font-size: 34px; line-height: 1.3; }
        #mo-wall { position: absolute; left: 735px; top: 205px; width: 1160px; height: 700px; perspective: 2200px; }
        #mo-plane { position: absolute; inset: 0; transform-style: preserve-3d; transform-origin: 30% 50%; }
        .mo-cell { position: absolute; width: 360px; height: 290px; }
        .mo-card { border-width: 6px; border-radius: 20px; }
        .mo-tag { position: absolute; left: 14px; top: 196px; font-size: 28px; box-shadow: 0 8px 16px rgba(70, 60, 20, 0.2); }
"""
html = f"""
          <div id="mo-head" class="t-chapter">{words("And a lot")} <span class="blue">{words("more.")}</span></div>
          <div id="mo-sub" class="t-line">History, tickets, checksums and the tools you reach for.</div>
          <div id="mo-wall"><div id="mo-plane">{cards}
          </div></div>"""
js = """
        tl.fromTo("#mo-head .w", { opacity: 0, y: 26, filter: "blur(10px)" }, { opacity: 1, y: 0, filter: "blur(0px)", duration: 0.85, ease: "expo.out", stagger: 0.05 }, 0.1);
        tl.fromTo("#mo-sub", { opacity: 0, y: 18, filter: "blur(6px)" }, { opacity: 1, y: 0, filter: "blur(0px)", duration: 0.8, ease: "expo.out" }, 0.55);
        tl.fromTo("#mo-plane", { rotationY: -12, rotationX: 5 }, { rotationY: -6, rotationX: 3, duration: 4, ease: "sine.inOut" }, 0);
        tl.fromTo(".mo-cell", { y: 60, opacity: 0, filter: "blur(8px)" }, { y: 0, opacity: 1, filter: "blur(0px)", duration: 0.8, ease: "expo.out", stagger: 0.07 }, 0.15);
        tl.to(".mo-cell", { y: "-=10", duration: 1.5, ease: "sine.inOut", yoyo: true, repeat: 1, stagger: 0.15 }, 1.0);
        tl.to("#mo-head, #mo-sub, #mo-wall", { yPercent: -8, opacity: 0, filter: "blur(6px)", duration: 0.32, ease: "power2.in", stagger: 0.025 }, 3.66);"""
scenes.append((*scene(13, "more", 4, "A labelled grid of moving screens: History, Tickets, Hex view, SHA-256, Settings, Report an issue.", css, html, js), 62, 4))

# 13 versus: what carries on from FBI, and what's new. No marks against FBI.
rows = [("Install CIAs and scan QR codes", "improved"),
        ("Queue a whole page of QR codes", "new"),
        ("Resume a dropped download", "new"),
        ("Manage the SD card from your phone", "new")]
rh = ""
for i, (r, kind) in enumerate(rows):
    y = 370 + i * 120
    rh += f"""
          <div class="row v-row" style="top:{y}px"><span>{r}</span></div>
          <div class="v-tag v-{kind}" style="top:{y + 26}px">{"Improved" if kind == "improved" else "New"}</div>"""
css = """
        #v-title { position: absolute; left: 0; right: 0; top: 110px; text-align: center; }
        #v-title .t-story { font-size: 76px; }
        #v-note { position: absolute; left: 0; right: 0; top: 920px; text-align: center; font-size: 34px; }
        #v-panel { position: absolute; left: 260px; top: 330px; width: 1400px; height: 560px; }
        .v-row { position: absolute; left: 300px; width: 1320px; height: 100px; padding-left: 40px; font-size: 42px; }
        .v-tag { position: absolute; left: 1400px; width: 180px; height: 48px; border-radius: 12px; display: flex; align-items: center;
          justify-content: center; font-family: "M PLUS Rounded 1c", sans-serif; font-weight: 800; font-size: 30px; color: #fff; }
        .v-improved { background: #609cb4; }
        .v-new { background: #cc7c0b; box-shadow: 0 0 0 5px #ffe2a8; }
"""
html = f"""
          <div id="v-title"><div class="t-story">{words("The basics you know,")} <span class="blue">{words("and a lot that's new.")}</span></div></div>
          <div id="v-panel" class="panel"></div>{rh}
          <div id="v-note" class="t-line">FBI's servefiles.py, sendurls.py and Boop still work.</div>"""
js = """
        tl.fromTo("#v-title .w", { opacity: 0, y: 26, filter: "blur(10px)" }, { opacity: 1, y: 0, filter: "blur(0px)", duration: 0.85, ease: "expo.out", stagger: 0.05 }, 0.1);
        tl.fromTo("#v-panel", { y: 60, scale: 0.97, opacity: 0, filter: "blur(8px)" }, { y: 0, scale: 1, opacity: 1, filter: "blur(0px)", duration: 0.85, ease: "expo.out" }, 0.12);
        tl.fromTo(".v-row", { y: 22, opacity: 0, filter: "blur(6px)" }, { y: 0, opacity: 1, filter: "blur(0px)", duration: 0.6, ease: "expo.out", stagger: 0.5 }, 0.75);
        tl.fromTo(".v-tag", { scale: 0 }, { scale: 1, duration: 0.35, ease: "back.out(2.6)", stagger: 0.5 }, 0.95);
        tl.fromTo("#v-note", { opacity: 0, y: 18, filter: "blur(6px)" }, { opacity: 1, y: 0, filter: "blur(0px)", duration: 0.7, ease: "expo.out" }, 2.6);
        tl.to("#v-title, #v-panel, .v-row, .v-tag, #v-note", { yPercent: -6, opacity: 0, filter: "blur(6px)", duration: 0.32, ease: "power2.in" }, 3.66);"""
scenes.append((*scene(14, "versus", 4, "What carries on from FBI and what's new. Every row is from director/PRODUCT.md; no marks against FBI.", css, html, js), 66, 4))

# 14 end
css = """
        #e-icon { position: absolute; left: 160px; top: 150px; width: 170px; height: 170px; border-radius: 40px;
          box-shadow: 0 24px 50px rgba(70, 60, 20, 0.24); }
        #e-word { position: absolute; left: 150px; top: 340px; display: flex; font-size: 150px; }
        #e-word span { display: block; }
        #e-line { position: absolute; left: 160px; top: 540px; }
        #e-line .t-story { font-size: 70px; }
        #e-url { position: absolute; left: 160px; top: 760px; font-size: 46px; color: #17799f; }
        #e-tags { position: absolute; left: 160px; top: 840px; display: flex; gap: 14px; }
        #e-qr { position: absolute; left: 1180px; top: 230px; width: 480px; height: 480px; border-radius: 36px; background: #fff;
          box-shadow: 0 34px 70px rgba(70, 60, 20, 0.26); display: flex; align-items: center; justify-content: center; }
        #e-qr img { width: 400px; height: 400px; }
        #e-cap { position: absolute; left: 1000px; width: 840px; top: 752px; text-align: center; font-size: 34px; }
        #e-console { position: absolute; inset: 0; }
        #e-console video { position: absolute; inset: 0; width: 1920px; height: 1080px; }
        #e-credit { position: absolute; left: 0; right: 0; top: 1028px; text-align: center; font-size: 14px; letter-spacing: 0.01em; color: #716a52; }
"""
html = f"""
          <img id="e-icon" src="assets/brand/icon-1024.png" alt="" />
          <div id="e-word" class="t-hero"><span>3</span><span>d</span><span>s</span><span class="blue">D</span><span class="blue">L</span></div>
          <div id="e-line"><div class="t-story">{words("Scan it in FBI.")}</div><div class="t-story blue">{words("One last time.")}</div></div>
          <div id="e-url" class="t-code">ankush.one/3dsDL</div>
          <div id="e-tags"><div class="tag good">Free</div><div class="tag">Open source</div><div class="tag">GPLv3</div></div>
          <div id="e-qr"><img src="assets/qr/install-cia.svg" alt="" /></div>
          <div id="e-cap" class="t-line">In FBI: Remote Install, then Scan QR Code</div>
          <div id="e-console"><video id="e-console-v" src="assets/footage/console-close.webm" muted playsinline data-start="5.4" data-duration="2.6" data-media-start="0" data-track-index="1"></video></div>
          <div id="e-credit" class="t-small">3DS photo by PantheraLeo1359531 (CC BY 4.0). 3D model "New Nintendo 3DS" by Geng4d. Music: "Home Menu" and "Software Download" (Nintendo eShop) from the Nintendo 3DS system, © Nintendo. Not affiliated with Nintendo.</div>"""
js = """
        tl.fromTo("#e-icon", { scale: 0, rotation: -12 }, { scale: 1, rotation: 0, duration: 0.7, ease: "back.out(2.2)" }, 0.1);
        tl.fromTo("#e-word span", { y: 40, opacity: 0, filter: "blur(10px)" }, { y: 0, opacity: 1, filter: "blur(0px)", duration: 0.85, ease: "expo.out", stagger: 0.05 }, 0.3);
        tl.fromTo("#e-qr", { y: 60, scale: 0.96, opacity: 0, filter: "blur(8px)" }, { y: 0, scale: 1, opacity: 1, filter: "blur(0px)", duration: 0.85, ease: "expo.out" }, 0.15);
        tl.fromTo("#e-line .w", { opacity: 0, y: 26, filter: "blur(10px)" }, { opacity: 1, y: 0, filter: "blur(0px)", duration: 0.85, ease: "expo.out", stagger: 0.05 }, 0.9);
        tl.fromTo("#e-cap", { opacity: 0, y: 18, filter: "blur(6px)" }, { opacity: 1, y: 0, filter: "blur(0px)", duration: 0.7, ease: "expo.out" }, 1.4);
        tl.fromTo("#e-url", { opacity: 0, y: 18, filter: "blur(6px)" }, { opacity: 1, y: 0, filter: "blur(0px)", duration: 0.7, ease: "expo.out" }, 1.8);
        tl.fromTo("#e-tags .tag", { scale: 0, opacity: 0 }, { scale: 1, opacity: 1, duration: 0.3, ease: "back.out(2.4)", stagger: 0.1 }, 2.1);
        tl.fromTo("#e-credit", { opacity: 0 }, { opacity: 1, duration: 0.6 }, 2.6);
        tl.to("#e-qr", { y: -10, duration: 1.6, ease: "sine.inOut", yoyo: true, repeat: 1 }, 1.1);
        tl.to("#e-icon, #e-word, #e-line, #e-url, #e-tags, #e-qr, #e-cap", { yPercent: -8, opacity: 0, filter: "blur(6px)", duration: 0.34, ease: "power2.in", stagger: 0.02 }, 5.0);
        tl.fromTo("#e-console", { opacity: 0, scale: 1.04 }, { opacity: 1, scale: 1, duration: 0.4, ease: "power2.out" }, 5.5);"""
scenes.append((*scene(15, "end", 8, "End card: icon, wordmark, the line, the real install QR with the ring on it.", css, html, js), 70, 8))

# ================================================================ host
RAIL = [("Scan", C["blue"], "Scan", "a QR code, a link, or your phone"),
        ("Download", C["green"], "Speed", "split across four connections"),
        ("Package", C["blue"], "Install", "CIAs, seeds included"),
        ("Heart", C["red"], "Little things", "done right, everywhere"),
        ("Folder", C["orange"], "Files", "browse, preview, edit"),
        ("GameCard", C["gold"], "Titles &amp; saves", "backups Checkpoint can read"),
        ("Chip", C["pink"], "Receive", "links from your phone"),
        ("Globe", C["teal"], "Files over Wi-Fi", "SD card in a browser")]
CH = [16, 24, 28, 32, 36, 44, 48, 52]  # when each rail item becomes current
PITCH = 230
rail_html = "\n".join(f"""          <div class="rail-tile" id="rail-t{i}" style="top:{435 + i * PITCH}px">
            <div class="rail-ico">{icon(n, c, 78)}</div>
            <div class="rail-txt"><div class="rail-title"{' style="font-size:40px"' if len(t) > 14 else ''}>{t}</div><div class="rail-sub">{s}</div></div>
          </div>""" for i, (n, c, t, s) in enumerate(RAIL))

clips = "\n".join(f'      <div id="{cid}" data-composition-id="{cid}" data-composition-src="{src}" data-start="{st}" data-duration="{d}" data-track-index="{2 + (k % 2)}" data-width="1920" data-height="1080"></div>'
                  for k, (cid, src, st, d) in enumerate(scenes))

ring_js = []
R = ring_js.append
R('tl.set("#ring", { left: 822, top: 192, width: 276, height: 276, borderRadius: 66, borderWidth: 9, rotation: 0, skewX: 0, opacity: 0 }, 0);')
R('tl.fromTo("#ring", { scale: 1.6, opacity: 0 }, { scale: 1, opacity: 1, duration: 0.45, ease: "back.out(2.2)", immediateRender: false }, 8.05);')
R('tl.to("#ring", { opacity: 0.7, duration: 0.6, ease: "sine.inOut", yoyo: true, repeat: 3 }, 8.6);')
# to the Scan QR Code tile on the console's touch screen
R('tl.to("#ring", { left: 1038, top: 580, width: 112, height: 78, borderRadius: 14, borderWidth: 4, rotation: 23, skewX: -33.7, opacity: 1, duration: 0.8, ease: "power3.inOut" }, 12.2);')
R('tl.fromTo("#ring", { scale: 1.25 }, { scale: 1, duration: 0.3, ease: "back.out(2.5)" }, 14.3);')
# lifts off and becomes the menu's cursor
R('tl.to("#ring", { left: 86, top: 421, width: 568, height: 238, borderRadius: 40, borderWidth: 9, rotation: 0, skewX: 0, duration: 0.7, ease: "power3.inOut" }, 15.0);')
for i, t in enumerate(CH):
    if i:
        R(f'tl.fromTo("#ring", {{ scaleY: 0.93, scaleX: 1.02 }}, {{ scaleY: 1, scaleX: 1, duration: 0.3, ease: "back.out(2)" }}, {t - 0.02});')
    R(f'tl.fromTo("#ring", {{ scale: 1.05 }}, {{ scale: 1, duration: 0.25, ease: "back.out(2)" }}, {t + 0.35});')
R('tl.to("#ring", { left: -640, opacity: 0, duration: 0.45, ease: "power3.in" }, 62.0);')
# the 3dsDL column in the comparison
# then around the install QR
R('tl.set("#ring", { left: 1166, top: 216, width: 508, height: 508, borderRadius: 46 }, 69.6);')
R('tl.fromTo("#ring", { scale: 1.15, opacity: 0 }, { scale: 1, opacity: 1, duration: 0.45, ease: "back.out(2.2)", immediateRender: false }, 70.5);')
R('tl.to("#ring", { opacity: 0.6, duration: 0.6, ease: "sine.inOut", yoyo: true, repeat: 3 }, 71.4);')
R('tl.to("#ring", { scale: 1.06, opacity: 0, duration: 0.38, ease: "power2.in" }, 75.0);')

ring_js = [l.replace("left:", "x:").replace("top:", "y:") for l in ring_js]
rail_js = []
Q = rail_js.append
# The last tile's New badge lives on a layer above the ring (#rail-o7) and
# copies the tile's emphasis.
rsel = lambda i: f"#rail-t{i}" + (", #rail-o7" if i == 7 else "")
Q('tl.fromTo("#rail, #rail-o", { x: -720 }, { x: 0, duration: 0.7, ease: "power3.out" }, 15.2);')
# Every tile's emphasis as explicit from/to pairs, so any seek order lands right.
for i in range(len(RAIL)):
    on = CH[i]
    if i:
        Q(f'tl.fromTo("{rsel(i)}", {{ opacity: 0.32, scale: 0.9 }}, {{ opacity: 1, scale: 1, duration: 0.3, ease: "power2.out", immediateRender: false }}, {on - 0.15});')
    else:
        Q('tl.fromTo("#rail-t0", { opacity: 1, scale: 1 }, { opacity: 1, scale: 1, duration: 0.01 }, 15.2);')
    Q(f'tl.fromTo("{rsel(i)}", {{ scale: 0.96 }}, {{ scale: 1, duration: 0.22, ease: "back.out(2)", immediateRender: false }}, {on + 0.35});')
    if i + 1 < len(RAIL):
        Q(f'tl.fromTo("#rail-t{i}", {{ opacity: 1, scale: 1 }}, {{ opacity: 0.32, scale: 0.9, duration: 0.3, ease: "power2.out", immediateRender: false }}, {CH[i + 1] - 0.2});')
        Q(f'tl.fromTo("#rail-list, #rail-olist", {{ y: {-PITCH * i} }}, {{ y: {-PITCH * (i + 1)}, duration: 0.38, ease: "power3.inOut", immediateRender: false }}, {CH[i + 1] - 0.2});')
Q('tl.to("#rail, #rail-o", { x: -720, duration: 0.45, ease: "power3.in" }, 62.0);')

ground_js = """tl.fromTo("#g-glow1", { x: -120, y: 40 }, { x: 260, y: -60, duration: 78, ease: "sine.inOut" }, 0);
        tl.fromTo("#g-glow2", { x: 160, y: -40 }, { x: -300, y: 80, duration: 78, ease: "sine.inOut" }, 0);
        tl.fromTo(".g-ghost", { x: 0 }, { x: (i) => [-180, -260, -120, -220][i], duration: 78, ease: "none" }, 0);
        tl.fromTo("#g-dots", { y: 0 }, { y: -52, duration: 78, ease: "none" }, 0);
        tl.fromTo("#g-flash", { opacity: 0 }, { opacity: 1, duration: 0.3, ease: "power2.in" }, 7.7);
        tl.to("#g-flash", { opacity: 0, duration: 0.38, ease: "power2.out" }, 8.0);
        tl.fromTo("#g-dim", { opacity: 0 }, { opacity: 0.9, duration: 1.0, ease: "power2.in" }, 77.0);"""

index = f"""<!doctype html>
<html lang="en" data-resolution="landscape">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=1920, height=1080" />
    <title>3dsDL: Follow the ring</title>
    <script src="https://cdn.jsdelivr.net/npm/gsap@3.14.2/dist/gsap.min.js"></script>
    <link rel="stylesheet" href="assets/css/system.css" />
    <style>
      @font-face {{ font-family: "M PLUS Rounded 1c"; font-weight: 700; src: url("assets/fonts/m-plus-rounded-1c-700-latin.woff2") format("woff2"); }}
      @font-face {{ font-family: "M PLUS Rounded 1c"; font-weight: 800; src: url("assets/fonts/m-plus-rounded-1c-800-latin.woff2") format("woff2"); }}
      @font-face {{ font-family: "Gaegu"; font-weight: 700; src: url("assets/fonts/gaegu-700-latin.woff2") format("woff2"); }}
      @font-face {{ font-family: "M PLUS 1 Code"; font-weight: 500; src: url("assets/fonts/m-plus-1-code-500-latin.woff2") format("woff2"); }}
      #ring {{ left: 0; top: 0; }}
      * {{ margin: 0; padding: 0; box-sizing: border-box; }}
      html, body {{ margin: 0; width: 1920px; height: 1080px; overflow: hidden; background: #f2e9a3; }}
      #root {{ position: relative; width: 100%; height: 100%; overflow: hidden; background: #f2e9a3; }}
      #ground {{ z-index: 0; }}
      #g-glow1 {{ width: 900px; height: 900px; left: 200px; top: 80px; }}
      #g-glow2 {{ width: 760px; height: 760px; left: 1100px; top: 360px; }}
      .g-ghost {{ position: absolute; }}
      #rail {{ z-index: 20; }}
      #rail-list {{ position: absolute; left: 0; top: 0; width: 760px; height: 1080px; }}
      .rail-tile {{ opacity: 0.32; position: absolute; left: 100px; width: 540px; height: 210px; display: flex; align-items: center; gap: 22px;
        padding: 0 30px; border-radius: 30px; transform-origin: 0 50%;
        background: linear-gradient(180deg, #fefcf5 0%, #e0dccb 85%, #b6b192 100%);
        border-bottom: 3px solid #8f8659; box-shadow: 0 16px 30px rgba(70, 60, 20, 0.16); }}
      #rail-t0 {{ opacity: 1; }}
      .rail-new {{ position: absolute; right: -14px; top: -14px; padding: 6px 16px; border-radius: 12px; background: #cc7c0b; color: #fff;
        font-family: "M PLUS Rounded 1c", sans-serif; font-weight: 800; font-size: 26px; box-shadow: 0 0 0 4px #ffe2a8; }}
      .rail-ico {{ flex: none; width: 112px; height: 112px; border-radius: 28px; background: #fffdf0; display: flex;
        align-items: center; justify-content: center; box-shadow: inset 0 0 0 2px #e6dfb4; }}
      .rail-title {{ font-family: "M PLUS Rounded 1c", sans-serif; font-weight: 800; font-size: 48px; line-height: 1.05; color: #615a4b; white-space: nowrap; }}
      .rail-sub {{ margin-top: 8px; font-family: "M PLUS Rounded 1c", sans-serif; font-weight: 700; font-size: 30px; line-height: 1.2; color: #756e60; }}
      #ring-layer {{ z-index: 30; }}
      #rail-o {{ z-index: 32; pointer-events: none; }}
      #rail-olist {{ position: absolute; left: 0; top: 0; width: 760px; height: 1080px; }}
      .rail-tile-o {{ position: absolute; left: 100px; width: 540px; height: 210px; transform-origin: 0 50%; opacity: 0.32; }}
      #g-dim {{ z-index: 40; background: #141208; }}
      #g-flash {{ z-index: 35; background: #fffdf0; }}
    </style>
  </head>
  <body>
    <!-- "Follow the ring": the 3dsDL film. director/FILM-PLAN.md has the idea
         and the shot list; tools/score/timeline.ts has the bars. The ground,
         the chapter menu (rail) and the green ring live here, on the main
         timeline, so they carry across the cuts between scenes. -->
    <div id="root" data-composition-id="main" data-start="0" data-duration="78" data-width="1920" data-height="1080">
      <div id="ground" class="clip ground" data-start="0" data-duration="78" data-track-index="0" data-layout-allow-overflow>
        <div id="g-glow1" class="glow sky"></div>
        <div id="g-glow2" class="glow mint"></div>
        <div class="ghost g-ghost" style="left:1500px;top:-60px;width:520px;height:320px"></div>
        <div class="ghost g-ghost" style="left:-120px;top:780px;width:620px;height:380px"></div>
        <div class="ghost g-ghost" style="left:1700px;top:820px;width:420px;height:260px"></div>
        <div class="ghost g-ghost" style="left:640px;top:-200px;width:480px;height:280px"></div>
        <div id="g-dots" class="dots"></div>
      </div>

{clips}

      <div id="rail" class="clip" data-start="15.2" data-duration="47.4" data-track-index="4" data-layout-allow-overflow>
        <div id="rail-list">
{rail_html}
        </div>
      </div>

      <div id="rail-o" class="clip" data-start="15.2" data-duration="47.4" data-track-index="8" data-layout-allow-overflow>
        <div id="rail-olist">
          <div class="rail-tile-o" id="rail-o7" style="top:{435 + 7 * PITCH}px"><div class="rail-new">New</div></div>
        </div>
      </div>

      <div id="ring-layer" class="clip" data-start="6" data-duration="72" data-track-index="5" data-layout-allow-overflow>
        <div id="ring" class="ring"></div>
      </div>

      <div id="g-flash" class="clip" data-start="7.7" data-duration="0.7" data-track-index="6"></div>

      <div id="g-dim" class="clip" data-start="76.9" data-duration="1.1" data-track-index="6"></div>

      <audio id="soundtrack" src="assets/music/film-mix.wav" data-start="0" data-duration="78" data-track-index="7" data-volume="1"></audio>
    </div>
    <script>
      const tl = gsap.timeline({{ paused: true }});
        {ground_js}
        {chr(10).join("        " + l for l in ring_js)}
        {chr(10).join("        " + l for l in rail_js)}
      window.__timelines["main"] = tl;
    </script>
  </body>
</html>
"""
open(os.path.join(ROOT, "index.html"), "w").write(index)

# ================================================================ cues (60 fps frames)
F = lambda s: round(s * 60)
cues = []
def cue(s, kind, gain=1.0, pan=0.0):
    c = {"frame": F(s), "kind": kind}
    if gain != 1.0: c["gain"] = gain
    if pan: c["pan"] = pan
    cues.append(c)

# Whooshes only where something big moves: the push into the screen, the
# first file crossing to the 3DS, the run into the ending.
cue(2.0, "pop", 0.35)
cue(4.0, "riser", 0.9); cue(4.0, "lid", 0.9); cue(5.35, "boot", 0.8); cue(7.6, "whoosh", 0.6)
cue(8.0, "impact"); cue(8.12, "pop", 0.5, -0.4); cue(8.22, "pop", 0.5, 0.4)
for i in range(5): cue(8.3 + 0.07 * i, "type", 0.5)
cue(12.95, "move"); cue(14.3, "tap")
for t in CH:
    if t != 16: cue(t - 0.02, "move")
    cue(t + 0.35, "select", 0.8)
cue(19.0, "chime", 0.8); cue(21.0, "tap", 0.8); cue(22.25, "pop", 0.6)
cue(26.2, "done", 0.7)
cue(28.6, "tap", 0.7); cue(30.8, "installed", 0.8)
for i in range(6): cue(32.3 + 0.45 * i, "pop", 0.55, [-0.3, 0, 0.3][i % 3])
for t in [37.9, 39.3, 41.85]: cue(t, "move", 0.8)
cue(41.5, "type", 0.5); cue(41.75, "type", 0.5); cue(42.0, "done", 0.6)
cue(50.0, "notify", 0.8)
W0, wt2 = 52, lambda m: round((m - 9.2) / 1.06, 2)
cue(W0 + wt2(9.65), "tap", 0.6); cue(W0 + wt2(11.0), "pop", 0.6); cue(W0 + wt2(11.0) + 0.15, "whoosh", 0.4)
cue(W0 + wt2(14.0), "tap", 0.6); cue(W0 + wt2(14.3), "notify", 0.8); cue(W0 + wt2(16.0), "installed", 0.7)
cue(W0 + wt2(18.2), "tap", 0.5); cue(W0 + wt2(19.2), "done", 0.6)
for i in range(4): cue(66.95 + 0.5 * i, "select", 0.6)
cue(69.6, "whoosh", 0.5); cue(70.55, "chime", 0.8); cue(70.6, "impact", 0.8)
cue(75.8, "lid", 0.9)
cues.sort(key=lambda c: c["frame"])
ts = "// Every sound in the film, frames at 60 fps (seconds x 60). Written with the\n// first build of the film; edit freely, then run `npm run score`.\nexport const CUES: {frame: number; kind: string; gain?: number; pan?: number}[] = [\n"
ts += "".join("  " + json.dumps(c).replace('"frame"', 'frame').replace('"kind"', 'kind').replace('"gain"', 'gain').replace('"pan"', 'pan') + ",\n" for c in cues)
ts += "];\n"
open(os.path.join(ROOT, "tools/score/cue-sheet.ts"), "w").write(ts)
print(len(scenes), "scenes,", len(cues), "cues")
