#!/usr/bin/env python3
"""Converts banner.glb to the CGFX model the HOME Menu banner holds.

pycgfx (https://github.com/skyfloogle/pycgfx) does the conversion; this
wrapper then adjusts two things it leaves at defaults:

- Its one light shines straight from the camera, and materials get no
  ambient light, so anything turned away from the viewer goes black. The
  light is moved up and to the left, like a studio key light, and every
  material gets a soft ambient fill.
- The logo is a picture, not a surface, so it gets full ambient light and
  shows its own colours whatever the angle.

scripts/make-banner.sh runs this in a pinned Python container with pycgfx
fetched at a pinned commit.

Usage: python3 build-cgfx.py PYCGFX_DIR banner.glb 3dsDL.cgfx
"""

import math
import sys

LIMIT = 0x80000  # the HOME Menu refuses CGFX files above 512 KiB
AMBIENT = 0.42
LIGHT = (0.35, -0.5, -0.8)  # direction the light travels: down, right, into the screen


def main(pycgfx_dir, gltf_path, out_path):
    sys.path.insert(0, pycgfx_dir)
    import gltflib
    import main as pycgfx
    from cgfx.shared import ColorFloat, Vector3

    cgfx = pycgfx.convert_gltf(gltflib.GLTF.load(gltf_path, load_file_resources=True))

    model = cgfx.data.models["COMMON"]
    for name in model.materials:
        level = 1.0 if name == "Logo" else AMBIENT
        model.materials[name].material_color.ambient = ColorFloat(level, level, level, 1)

    length = math.sqrt(sum(c * c for c in LIGHT))
    for name in cgfx.data.lights:
        cgfx.data.lights[name].position_or_direction = Vector3(*(c / length for c in LIGHT))

    data = pycgfx.write(cgfx)
    if len(data) > LIMIT:
        sys.exit("CGFX is %d bytes; the HOME Menu's limit is %d" % (len(data), LIMIT))
    with open(out_path, "wb") as out:
        out.write(data)
    print("%s: %d bytes (limit %d), materials: %s" % (out_path, len(data), LIMIT, ", ".join(model.materials)))


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
