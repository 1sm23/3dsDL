"""The white console, from Geng4d's "New Nintendo 3DS" (Sketchfab, bought;
assets/models/geng4d-new3ds, kept out of git). It is fully textured, so it
keeps its own materials; only the Pikachu cover plates turn plain white and
the app's screens go onto the glass.

build() returns the same rig as console.build():
    rig.set_lid(f), rig.set_screens(v), rig.set_led(v), rig.top_screen
The console is 1 unit wide, centred, standing on z = 0, front toward -Y.
"""
import math
import os

import bmesh
import bpy
from mathutils import Matrix, Vector

import scene as S

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL = os.path.join(HERE, "..", "..", "assets", "models", "geng4d-new3ds", "New_Nintendo_3DS.gltf")
# The lid node's own origin is the hinge: identity is shut (the file's black
# console), and 110 degrees open matches the three.js pose (70 above the back).
OPEN_TURN = 110.0
# The top LCD inside "Top Screen/Body"'s texture, in Blender UV space.
LCD_UV = (0.195, 0.311, 0.806, 0.681)


class Rig:
    pass


def subtree(o):
    out = [o]
    for c in o.children:
        out += subtree(c)
    return out


def world_box(objs):
    lo = Vector((1e9, 1e9, 1e9))
    hi = -lo
    for o in objs:
        if o.type != "MESH":
            continue
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w))
            hi = Vector(map(max, hi, w))
    return lo, hi


def lcd_material(m, image_path):
    """The lid's own material, with the app's top screen lit inside the LCD rectangle."""
    nt = m.node_tree
    bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
    uv = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(uv.outputs["UV"], sep.inputs[0])
    u0, v0, u1, v1 = LCD_UV
    mapping = nt.nodes.new("ShaderNodeMapping")
    mapping.inputs["Location"].default_value = (-u0 / (u1 - u0), -v0 / (v1 - v0), 0)
    mapping.inputs["Scale"].default_value = (1 / (u1 - u0), 1 / (v1 - v0), 1)
    nt.links.new(uv.outputs["UV"], mapping.inputs["Vector"])
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(image_path)
    tex.interpolation = "Cubic"
    tex.extension = "CLIP"
    nt.links.new(mapping.outputs["Vector"], tex.inputs["Vector"])

    def inside(ch, lo, hi):
        a = nt.nodes.new("ShaderNodeMath"); a.operation = "GREATER_THAN"; a.inputs[1].default_value = lo
        b = nt.nodes.new("ShaderNodeMath"); b.operation = "LESS_THAN"; b.inputs[1].default_value = hi
        nt.links.new(sep.outputs[ch], a.inputs[0]); nt.links.new(sep.outputs[ch], b.inputs[0])
        m_ = nt.nodes.new("ShaderNodeMath"); m_.operation = "MULTIPLY"
        nt.links.new(a.outputs[0], m_.inputs[0]); nt.links.new(b.outputs[0], m_.inputs[1])
        return m_
    mask = nt.nodes.new("ShaderNodeMath"); mask.operation = "MULTIPLY"
    nt.links.new(inside("X", u0, u1).outputs[0], mask.inputs[0])
    nt.links.new(inside("Y", v0, v1).outputs[0], mask.inputs[1])
    strength = nt.nodes.new("ShaderNodeMath"); strength.operation = "MULTIPLY"
    strength.inputs[1].default_value = 0.0
    nt.links.new(mask.outputs[0], strength.inputs[0])
    emit = nt.nodes.new("ShaderNodeEmission")
    nt.links.new(tex.outputs["Color"], emit.inputs["Color"])
    nt.links.new(strength.outputs[0], emit.inputs["Strength"])
    add = nt.nodes.new("ShaderNodeAddShader")
    nt.links.new(bsdf.outputs["BSDF"], add.inputs[0])
    nt.links.new(emit.outputs["Emission"], add.inputs[1])
    nt.links.new(add.outputs["Shader"], out.inputs["Surface"])
    m["emit_node"] = strength.name
    return m


def lcd_proxy(mesh_obj):
    """A hidden copy of the faces inside the LCD rectangle, for the camera push."""
    bm = bmesh.new()
    bm.from_mesh(mesh_obj.data)
    uvl = bm.loops.layers.uv.active
    u0, v0, u1, v1 = LCD_UV
    # Faces whose UV centre falls inside the LCD (some cross its edge).
    def inside(f):
        c = sum((l[uvl].uv for l in f.loops), Vector((0, 0))) / len(f.loops)
        return u0 <= c.x <= u1 and v0 <= c.y <= v1
    keep = [f for f in bm.faces if inside(f)]
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f not in set(keep)], context="FACES")
    me = bpy.data.meshes.new("lcd_proxy")
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new("lcd_proxy", me)
    bpy.context.scene.collection.objects.link(o)
    o.parent = mesh_obj
    o.matrix_parent_inverse = Matrix.Identity(4)
    o.hide_render = True
    return o


def build(look="geng4d-white"):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=MODEL)
    objs = [o for o in bpy.data.objects if o not in before]
    byname = {o.name: o for o in objs}
    # Only the white console and the cartridge in its slot: drop the black
    # console and the loose cartridge.
    for n in ("Body.001_55", "Game Cart.002_56"):
        for o in subtree(byname[n]):
            objs.remove(o)
            bpy.data.objects.remove(o)
    sk = byname["Sketchfab_model"]
    sk.location, sk.rotation_mode = (0, 0, 0), "QUATERNION"
    # Drop Sketchfab's display tilt and lay the console down, face up.
    sk.rotation_quaternion = Matrix.Rotation(math.radians(-90), 4, "X").to_quaternion()
    root = bpy.data.objects.new("console", None)
    bpy.context.scene.collection.objects.link(root)
    sk.parent = root
    lid = byname["Screen_18"]
    lid.rotation_mode = "QUATERNION"
    lid_rest = lid.rotation_quaternion.copy()

    def set_lid(f):
        lid.rotation_quaternion = Matrix.Rotation(-math.radians(OPEN_TURN * f), 4, "X").to_quaternion()
        bpy.context.view_layer.update()

    set_lid(0)
    lo, hi = world_box(objs)
    if hi.y - lo.y > hi.x - lo.x:  # stood the wrong way: turn it to face -Y
        root.rotation_euler = (0, 0, math.radians(90))
        bpy.context.view_layer.update()
        lo, hi = world_box(objs)
    k = 1 / (hi.x - lo.x)
    c = (lo + hi) / 2
    root.scale = (k, k, k)
    root.location = (-c.x * k, -c.y * k, -lo.z * k)
    bpy.context.view_layer.update()

    white = S.plastic("plate", (0.62, 0.59, 0.57), rough=0.32, coat=0.4, coat_rough=0.25, bevel=0.002, grain=0.03, bevel_samples=2)
    cart = S.plastic("cart", (0.3, 0.3, 0.3), rough=0.45, bevel=0.001, grain=0.02, bevel_samples=2)
    top_mesh = bot_mat = None
    for o in objs:
        if o.type != "MESH":
            continue
        for slot in o.material_slots:
            n = slot.material.name if slot.material else ""
            if "Pikachu" in n:
                slot.material = white
            elif n == "Light Gray Plastic.001":
                slot.material = cart  # the label carries game art: plain grey
            elif n.startswith("Top Screen/Body"):
                top_mesh = o
            elif n.startswith("Lower Screen"):
                S.planar_uvs(o)
                bot_mat = bot_mat or S.screen("bottom_screen", os.path.join(S.ASSETS, "home-bottom-hi.png"), 1.0)
                slot.material = bot_mat
    top_mat = lcd_material(top_mesh.material_slots[0].material, os.path.join(S.ASSETS, "home-top-hi.png"))

    rig = Rig()
    rig.top_screen = lcd_proxy(top_mesh)

    def set_screens(v):
        top_mat.node_tree.nodes[top_mat["emit_node"]].inputs[1].default_value = v
        bot_mat.node_tree.nodes[bot_mat["emit_node"]].inputs["Strength"].default_value = v

    rig.root, rig.objs, rig.lid = root, objs, lid
    rig.set_lid, rig.set_screens = set_lid, set_screens
    rig.set_led = lambda v: None  # the model has no power light
    rig.lid_rest = lid_rest
    set_lid(1)
    return rig
