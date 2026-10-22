"""A single high-resolution still of the open console, for the thumbnail.
    blender -b --factory-startup -P still.py -- <out.png> [azimuth] [samples]
azimuth: camera angle in degrees (+22 faces the console left, as in the film)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
import geng4d, scene as S  # noqa: E402
args = sys.argv[sys.argv.index("--") + 1:]
out = os.path.abspath(args[0]); az = float(args[1]) if len(args) > 1 else 22.0; samples = int(args[2]) if len(args) > 2 else 64
S.reset(); S.setup_render(width=2560, height=1440, samples=samples); S.studio(light=0.5, floor="#f1ecd6")
rig = geng4d.build(); rig.set_lid(1); rig.set_screens(1)
# SCREEN_TOP / SCREEN_BOTTOM put other screens on the console (default Home).
for env, name in (("SCREEN_TOP", "home-top-hi"), ("SCREEN_BOTTOM", "home-bottom-hi")):
    if os.environ.get(env):
        img = bpy.data.images.load(os.path.abspath(os.environ[env]))
        for m in bpy.data.materials:
            for n in (m.node_tree.nodes if m.use_nodes else []):
                if n.type == "TEX_IMAGE" and n.image and name in n.image.name:
                    n.image = img
cam = S.camera()
look = (0, 0.36, -0.1)
# CAM="distance,elevation" for a closer or lower shot.
d, el = (float(v) for v in os.environ.get("CAM", "2.95,18").split(","))
S.aim(cam, S.orbit(look, d, az, el), look, shift_px=0)
bpy.context.scene.render.filepath = out
bpy.ops.render.render(write_still=True)
print("STILL", out)
