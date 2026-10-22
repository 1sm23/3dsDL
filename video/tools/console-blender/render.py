"""Renders the film's two console shots in Cycles, frame by frame, as RGBA
PNGs (the background transparent, the console's shadow kept by the shadow
catcher): the camera orbit, lid spring, screens lighting up and the push
into the top screen.

    blender -b --factory-startup -P render.py -- <out-dir> [samples] [first] [last] [step] [fps]

At 60 fps (the default) frames 0-479 are shot A (the opening, 8 s) and
480-659 shot B (the ending, 3 s). render-console.sh turns them into the two
WebM files at 4K (RES=4k). Every frame is a pure function of its time, so
the 30 fps frames are exactly the even 60 fps frames.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import geng4d  # noqa: E402
import scene as S  # noqa: E402

args = sys.argv[sys.argv.index("--") + 1:]
out_dir = os.path.abspath(args[0])
samples = int(args[1]) if len(args) > 1 else 24
# +1: the console faces left, toward the film's words (and the thumbnail);
# -1: the earlier right-facing shots.
SIDE = float(os.environ.get("CONSOLE_SIDE", "1"))
first = int(args[2]) if len(args) > 2 else 0
last = int(args[3]) if len(args) > 3 else 659
step = int(args[4]) if len(args) > 4 else 1
FPS = int(args[5]) if len(args) > 5 else 60
SHOT_B = 8 * FPS
os.makedirs(out_dir, exist_ok=True)

def clamp01(x):
    return min(1.0, max(0.0, x))


def smooth(x):
    x = clamp01(x)
    return x * x * (3 - 2 * x)


def in_out(x):
    x = clamp01(x)
    return 4 * x * x * x if x < 0.5 else 1 - (-2 * x + 2) ** 3 / 2


def back_out(x, k=1.5):
    x = clamp01(x) - 1
    return 1 + (k + 1) * x ** 3 + k * x ** 2


def lerp(a, b, t):
    return a + (b - a) * t


def lerp3(a, b, t):
    return tuple(lerp(a[i], b[i], t) for i in range(3))


T0, T1, TB = (0, 0.06, 0), (0, 0.36, -0.1), (0, 0.22, -0.05)


def shot_a(t):
    lid = 0.0 if t < 4 else back_out((t - 4) / 1.0, 1.25)
    o = in_out((t - 3.3) / 1.6)
    az = SIDE * lerp(lerp(38, 28, in_out(t / 4)), 22, o)
    el = lerp(lerp(30, 24, in_out(t / 4)), 18, o)
    d = lerp(lerp(2.45, 2.25, in_out(t / 4)), 2.95, o)
    look = lerp3(T0, T1, o)
    return dict(lid=lid, screen=smooth((t - 5.35) / 0.3), led=smooth((t - 2.0) / 0.35),
                cam=S.orbit(look, d, az, el), look=look, shift=400,
                push=in_out((t - 5.7) / 2.3), push_in=clamp01((t - 7.55) / 0.45))


def shot_b(t):
    close = clamp01((t - 0.45) / 0.7)
    look = lerp3(TB, T0, in_out((t - 0.4) / 1.2))
    d = lerp(2.9, 2.5, in_out(t / 3))
    return dict(lid=1 - close ** 3, screen=1 - smooth((t - 0.75) / 0.25),
                led=1.0 if t < 1.3 else 0.55 + 0.45 * math.cos((t - 1.3) * 3.2),
                cam=S.orbit(look, d, SIDE * lerp(26, 32, in_out(t / 3)), lerp(20, 28, in_out(t / 3))),
                look=look, shift=0, push=0, push_in=0)


S.reset()
# RES=4k renders 3840 x 2160 for the 4K master (the camera shift is in
# 1080p pixels over 1920, so it holds at any size).
W, H = (3840, 2160) if os.environ.get("RES") == "4k" else (1920, 1080)
S.setup_render(width=W, height=H, samples=samples)
bpy.context.scene.render.use_persistent_data = True
S.studio(light=0.5, floor="#f1ecd6")
rig = geng4d.build()
if os.environ.get("FAST", "0") == "1":
    # Extra speed: decorrelated GPU sampling (scrambling distance) cuts GPU
    # time with no visible cost on a denoised product shot.
    sc = bpy.context.scene.cycles
    if hasattr(sc, "auto_scrambling_distance"):
        sc.auto_scrambling_distance = True
cam = S.camera()

lcd = rig.top_screen


def screen_frame():
    """The top screen's centre and the way it faces, in world space."""
    M = lcd.matrix_world
    pts = [M @ Vector(c) for c in lcd.bound_box]
    centre = sum(pts, Vector()) / 8
    n = (M.to_3x3() @ lcd.data.polygons[0].normal).normalized()
    if n.dot(Vector((0, -1, 0.3))) < 0:  # face the front of the console
        n = -n
    return centre, n


for i in range(first, last + 1, step):
    if os.path.exists(os.path.join(out_dir, f"f{i:04d}.png")):
        continue  # already rendered: re-running resumes
    s = shot_a(i / FPS) if i < SHOT_B else shot_b((i - SHOT_B) / FPS)
    rig.set_lid(s["lid"])
    rig.set_screens(s["screen"])
    rig.set_led(s["led"])
    cam_w, look_w = S.T(*s["cam"]), S.T(*s["look"])
    if s["push"] > 0:
        centre, n = screen_frame()
        end = centre + n * lerp(0.95, 0.42, s["push_in"] ** 2)
        cam_w = cam_w.lerp(end, s["push"])
        look_w = look_w.lerp(centre, s["push"])
    cam.location = cam_w
    cam.rotation_euler = (look_w - cam_w).to_track_quat("-Z", "Y").to_euler()
    cam.data.shift_x = -s["shift"] * (1 - s["push"]) / 1920
    bpy.context.scene.render.filepath = os.path.join(out_dir, f"f{i:04d}.png")
    bpy.ops.render.render(write_still=True)
    print(f"FRAME {i}", flush=True)
