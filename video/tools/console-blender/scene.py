"""Builds the 3dsDL console in Blender for Cycles: the model, physically based
materials, a product-photography light rig, a shadow catcher on a transparent
background, and a camera. Used by render.py (the film's two shots) and
still.py (thumbnail stills).

Coordinates: the three.js composition is y-up; Blender is z-up. A point
(x, y, z) in three.js is (x, -z, y) here. The console is scaled to 1 unit
wide and stands on z = 0.
"""
import math
import os

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")


def T(x, y, z):
    """three.js coordinates to Blender's."""
    return Vector((x, -z, y))


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


# ------------------------------------------------------------------ render

def setup_render(width=1920, height=1080, samples=96, percent=100):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    prefs = bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type = "METAL"
    prefs.refresh_devices()
    for d in prefs.devices:
        d.use = d.type == "METAL"  # the GPU alone; mixing in the CPU is slower
    sc.cycles.device = "GPU"
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.cycles.denoiser = "OPENIMAGEDENOISE"
    # Tuned for this scene (opaque plastic and screen glass on a floor):
    # short light paths, clamped indirect light so fewer samples come out
    # clean, and a guided GPU denoise that keeps edges and screen text sharp.
    sc.cycles.max_bounces = 4
    sc.cycles.diffuse_bounces = 2
    sc.cycles.glossy_bounces = 2
    sc.cycles.transmission_bounces = 2
    sc.cycles.transparent_max_bounces = 4
    sc.cycles.volume_bounces = 0
    sc.cycles.caustics_reflective = False
    sc.cycles.caustics_refractive = False
    sc.cycles.sample_clamp_indirect = 4.0
    sc.cycles.use_adaptive_sampling = True
    sc.cycles.adaptive_threshold = 0.03
    sc.cycles.adaptive_min_samples = 8
    # The FAST prefilter: ACCURATE looks the same here (clean albedo and
    # normal passes) but takes about 130 s a frame extra at 4K on the GPU.
    for attr, val in (("denoising_input_passes", "RGB_ALBEDO_NORMAL"), ("denoising_prefilter", "FAST"), ("denoising_use_gpu", True)):
        if hasattr(sc.cycles, attr):
            setattr(sc.cycles, attr, val)
    sc.render.resolution_x = width
    sc.render.resolution_y = height
    sc.render.resolution_percentage = percent
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    # Standard keeps the app's screen colours exactly as they are.
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.view_settings.exposure = 0.0


# --------------------------------------------------------------- materials

def node_mat(name):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    return m, nt, out


def plastic(name, color, rough=0.5, coat=0.0, coat_rough=0.3, bevel=0.006, grain=0.08, bevel_samples=4):
    """Moulded plastic: rounded edges from the Bevel node (so every hard
    edge catches light, as on the real console), a faint grain bump."""
    m, nt, out = node_mat(name)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Base Color"].default_value = (*color, 1)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Coat Weight"].default_value = coat
    bsdf.inputs["Coat Roughness"].default_value = coat_rough
    bev = nt.nodes.new("ShaderNodeBevel")
    bev.samples = bevel_samples
    bev.inputs["Radius"].default_value = bevel
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 900.0
    noise.inputs["Detail"].default_value = 2.0
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = grain
    bump.inputs["Distance"].default_value = 0.0005
    nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bev.outputs["Normal"], bump.inputs["Normal"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    return m


def screen(name, image_path, strength=1.0):
    """The app's screen as light, under a thin glossy glass coat."""
    m, nt, out = node_mat(name)
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(image_path)
    tex.interpolation = "Cubic"
    emit = nt.nodes.new("ShaderNodeEmission")
    emit.inputs["Strength"].default_value = strength
    nt.links.new(tex.outputs["Color"], emit.inputs["Color"])
    glass = nt.nodes.new("ShaderNodeBsdfPrincipled")
    glass.inputs["Base Color"].default_value = (0, 0, 0, 1)
    glass.inputs["Roughness"].default_value = 0.06
    # A faint sheen, not a mirror: the app has to stay readable.
    glass.inputs["Specular IOR Level"].default_value = 0.12
    add = nt.nodes.new("ShaderNodeAddShader")
    nt.links.new(emit.outputs["Emission"], add.inputs[0])
    nt.links.new(glass.outputs["BSDF"], add.inputs[1])
    nt.links.new(add.outputs["Shader"], out.inputs["Surface"])
    m["emit_node"] = emit.name
    return m


def simple(name, color, rough=0.4, metal=0.0, emit=None, emit_strength=0.0):
    m, nt, out = node_mat(name)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Base Color"].default_value = (*color, 1)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if emit:
        bsdf.inputs["Emission Color"].default_value = (*emit, 1)
        bsdf.inputs["Emission Strength"].default_value = emit_strength
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    return m


def srgb(hexstr):
    """'#rrggbb' to linear RGB."""
    h = hexstr.lstrip("#")
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


def planar_uvs(obj, flip_u=False, flip_v=False):
    """UVs straight across a screen's own extent, so the picture fills it."""
    me = obj.data
    xs = [v.co for v in me.vertices]
    ext = [max(c[i] for c in xs) - min(c[i] for c in xs) for i in range(3)]
    a, b = sorted(range(3), key=lambda i: -ext[i])[:2]
    lo = [min(c[i] for c in xs) for i in range(3)]
    if not me.uv_layers:
        me.uv_layers.new()
    uv = me.uv_layers.active.data
    for poly in me.polygons:
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            u = (co[a] - lo[a]) / (ext[a] or 1)
            v = (co[b] - lo[b]) / (ext[b] or 1)
            uv[li].uv = (1 - u if flip_u else u, 1 - v if flip_v else v)
    return a, b


# ------------------------------------------------------------------- model

def import_model(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    objs = [o for o in bpy.data.objects if o not in before]
    roots = [o for o in objs if o.parent is None]
    root = bpy.data.objects.new("console", None)
    bpy.context.scene.collection.objects.link(root)
    for r in roots:
        r.parent = root
    bpy.context.view_layer.update()
    meshes = [o for o in objs if o.type == "MESH"]
    lo = Vector((1e9, 1e9, 1e9))
    hi = -lo
    for o in meshes:
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w))
            hi = Vector(map(max, hi, w))
    size = hi - lo
    k = 1 / max(size.x, size.y)
    root.scale = (k, k, k)
    centre = (lo + hi) / 2
    root.location = (-centre.x * k, -centre.y * k, -lo.z * k)
    bpy.context.view_layer.update()
    return root, objs


# ------------------------------------------------------------------ lights

LIGHT_SCALE = [1.0]


def area(name, power, size_x, size_y, pos, target, color=(1, 1, 1), shadow=True):
    light = bpy.data.lights.new(name, "AREA")
    power *= LIGHT_SCALE[0]
    # Only the big soft lights cast shadows: thin strips and the front fill
    # would draw long hard-edged streaks across the shadow catcher.
    light.use_shadow = shadow
    light.shape = "RECTANGLE"
    light.size = size_x
    light.size_y = size_y
    light.energy = power
    light.color = color
    ob = bpy.data.objects.new(name, light)
    bpy.context.scene.collection.objects.link(ob)
    ob.location = pos
    d = Vector(target) - Vector(pos)
    ob.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    return ob


def studio(light=1.0, floor="#f2e9a3"):
    LIGHT_SCALE[0] = light
    # The same rig as the three.js version: a big soft key from the top left,
    # a top softbox, two thin strips behind for edge light.
    area("key", 190, 4.2, 3.0, T(-2.0, 2.4, 2.1), T(0, 0.15, 0), (1.0, 0.97, 0.92))
    area("top", 50, 2.4, 2.4, T(0.3, 3.0, -0.2), T(0, 0, 0))
    area("rim_r", 150, 0.22, 2.4, T(2.3, 1.0, -1.6), T(0, 0.2, 0), shadow=False)
    area("rim_l", 110, 0.22, 2.0, T(-2.5, 0.9, -1.3), T(0, 0.2, 0), (0.92, 0.96, 1.0), shadow=False)
    area("fill", 15, 3.0, 2.0, T(1.8, 0.8, 2.6), T(0, 0.2, 0), (1.0, 0.98, 0.93), shadow=False)
    # Reflections from a real studio, kept low; the background stays transparent.
    w = bpy.data.worlds.new("world")
    bpy.context.scene.world = w
    w.use_nodes = True
    nt = w.node_tree
    env = nt.nodes.new("ShaderNodeTexEnvironment")
    env.image = bpy.data.images.load(os.path.join(ASSETS, "studio_small_09_2k.hdr"))
    bg = nt.nodes["Background"]
    bg.inputs["Strength"].default_value = 0.12 * light
    nt.links.new(env.outputs["Color"], bg.inputs["Color"])
    # A floor that only catches the console's shadow.
    bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 0, 0))
    # Its colour still bounces up into the console: the film's cream, or a
    # paler one for a white shell, which would otherwise turn yellow.
    ob = bpy.context.active_object
    ob.name = "shadow_catcher"
    ob.is_shadow_catcher = True
    ob.data.materials.append(simple("floor", srgb(floor), 0.9))


# ------------------------------------------------------------------ camera

def camera(name="cam", fov_v_deg=26.0):
    cam = bpy.data.cameras.new(name)
    cam.sensor_fit = "VERTICAL"
    cam.angle_y = math.radians(fov_v_deg)
    cam.clip_start = 0.05
    ob = bpy.data.objects.new(name, cam)
    bpy.context.scene.collection.objects.link(ob)
    bpy.context.scene.camera = ob
    return ob


def orbit(target, d, az, el):
    """The three.js orbit (degrees), as a three.js-space point."""
    a, e = math.radians(az), math.radians(el)
    return (target[0] + math.sin(a) * math.cos(e) * d,
            target[1] + math.sin(e) * d,
            target[2] + math.cos(a) * math.cos(e) * d)


def aim(cam_ob, pos3, look3, shift_px=0.0):
    p, l = T(*pos3), T(*look3)
    cam_ob.location = p
    cam_ob.rotation_euler = (l - p).to_track_quat("-Z", "Y").to_euler()
    # three.js setViewOffset(-shift) slides the picture right by `shift`
    # pixels; Blender's shift moves the frame, so it is the opposite sign,
    # as a share of the 1920 px width.
    cam_ob.data.shift_x = -shift_px / 1920
