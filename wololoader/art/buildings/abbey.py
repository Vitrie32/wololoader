# Blender script: the "Abbey" building for the Monklanians example, rendered the way the
# original Age of Empires II sprites were made: a 3D model, orthographic 2:1 dimetric camera
# (elevation atan(1/2) = 26.565 deg, 45 deg around), one map tile = 96 px wide, sun from the
# upper left. Run:  blender -b -P abbey.py -- OUTDIR
# Writes OUTDIR/abbey_color.png (building, transparent background), OUTDIR/abbey_full.png
# (with ground shadow from a shadow catcher) and OUTDIR/abbey_info.txt (image size, hotspot).
import bpy, bmesh, math, sys, os
from mathutils import Vector, Matrix

OUT = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else "/tmp"
W, H = 340, 380          # sprite size (px)
SS = 2                   # rendered SS times bigger, then shrunk (sharper detail, thin outlines)
TILE_PX = 96.0           # one tile's diamond width on screen

bpy.ops.wm.read_factory_settings(use_empty=True)
scn = bpy.context.scene

# ---------- materials ----------
def mat(name, build):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bsdf.outputs[0], out.inputs[0])
    build(nt, bsdf)
    return m

def coord(nt, scale=1.0):
    # wall coordinates: u = x + y runs along any wall, v = z is the height, so bricks and roof
    # tiles lie in horizontal rows on every face (object coordinates gave vertical streaks)
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ"); nt.links.new(tc.outputs["Object"], sep.inputs[0])
    add = nt.nodes.new("ShaderNodeMath"); add.operation = "ADD"
    nt.links.new(sep.outputs[0], add.inputs[0]); nt.links.new(sep.outputs[1], add.inputs[1])
    comb = nt.nodes.new("ShaderNodeCombineXYZ")
    nt.links.new(add.outputs[0], comb.inputs[0]); nt.links.new(sep.outputs[2], comb.inputs[1])
    mp = nt.nodes.new("ShaderNodeMapping"); mp.inputs["Scale"].default_value = (scale, scale, scale)
    nt.links.new(comb.outputs[0], mp.inputs[0])
    return mp.outputs[0]

def grime(nt, color_socket, amount=0.35, scale=6.0):
    # multiply by low-frequency dirt so walls look weathered, like the hand-painted originals
    n = nt.nodes.new("ShaderNodeTexNoise"); n.inputs["Scale"].default_value = scale
    n.inputs["Detail"].default_value = 6
    tc = nt.nodes.new("ShaderNodeTexCoord"); nt.links.new(tc.outputs["Object"], n.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (1 - amount, 1 - amount * 1.05, 1 - amount * 1.15, 1)
    ramp.color_ramp.elements[1].color = (1, 1, 1, 1)
    nt.links.new(n.outputs["Fac"], ramp.inputs[0])
    mul = nt.nodes.new("ShaderNodeMix"); mul.data_type = "RGBA"; mul.blend_type = "MULTIPLY"
    mul.inputs["Factor"].default_value = 1.0
    nt.links.new(color_socket, mul.inputs[6]); nt.links.new(ramp.outputs[0], mul.inputs[7])
    return mul.outputs[2]

def bump(nt, bsdf, height_socket, strength=0.6):
    b = nt.nodes.new("ShaderNodeBump"); b.inputs["Strength"].default_value = strength
    b.inputs["Distance"].default_value = 0.02
    nt.links.new(height_socket, b.inputs["Height"]); nt.links.new(b.outputs[0], bsdf.inputs["Normal"])

def stone(nt, bsdf):   # warm sandstone blocks, weathered
    br = nt.nodes.new("ShaderNodeTexBrick")
    br.inputs["Color1"].default_value = (0.56, 0.43, 0.26, 1)
    br.inputs["Color2"].default_value = (0.44, 0.33, 0.19, 1)
    br.inputs["Mortar"].default_value = (0.17, 0.14, 0.10, 1)
    br.inputs["Scale"].default_value = 5.0
    br.inputs["Mortar Size"].default_value = 0.025
    br.inputs["Bias"].default_value = 0.3
    br.inputs["Row Height"].default_value = 0.22
    nt.links.new(coord(nt), br.inputs["Vector"])
    nt.links.new(grime(nt, br.outputs["Color"], 0.45), bsdf.inputs["Base Color"])
    bump(nt, bsdf, br.outputs["Fac"], 0.8)
    bsdf.inputs["Roughness"].default_value = 0.95

def roof(nt, bsdf):    # weathered terracotta tiles in rows
    br = nt.nodes.new("ShaderNodeTexBrick")
    br.inputs["Color1"].default_value = (0.55, 0.15, 0.05, 1)
    br.inputs["Color2"].default_value = (0.40, 0.10, 0.03, 1)
    br.inputs["Mortar"].default_value = (0.10, 0.04, 0.02, 1)
    br.inputs["Scale"].default_value = 16.0
    br.inputs["Mortar Size"].default_value = 0.04
    br.inputs["Brick Width"].default_value = 0.28
    br.inputs["Row Height"].default_value = 0.2
    nt.links.new(coord(nt), br.inputs["Vector"])
    nt.links.new(grime(nt, br.outputs["Color"], 0.5, 4.0), bsdf.inputs["Base Color"])
    bump(nt, bsdf, br.outputs["Fac"], 1.0)
    bsdf.inputs["Roughness"].default_value = 0.85

def planks(nt, bsdf):  # dark wooden boards
    w = nt.nodes.new("ShaderNodeTexWave"); w.inputs["Scale"].default_value = 18
    w.inputs["Distortion"].default_value = 4
    nt.links.new(coord(nt), w.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (0.10, 0.05, 0.02, 1)
    ramp.color_ramp.elements[1].color = (0.25, 0.13, 0.05, 1)
    nt.links.new(w.outputs["Fac"], ramp.inputs[0]); nt.links.new(ramp.outputs[0], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.9

def dirt(nt, bsdf):    # packed earth around the building
    n = nt.nodes.new("ShaderNodeTexNoise"); n.inputs["Scale"].default_value = 30; n.inputs["Detail"].default_value = 8
    tc = nt.nodes.new("ShaderNodeTexCoord"); nt.links.new(tc.outputs["Object"], n.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (0.20, 0.15, 0.09, 1)
    ramp.color_ramp.elements[1].color = (0.33, 0.26, 0.16, 1)
    nt.links.new(n.outputs["Fac"], ramp.inputs[0]); nt.links.new(ramp.outputs[0], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 1.0

def flat(rgb, rough=0.9):
    def f(nt, bsdf):
        bsdf.inputs["Base Color"].default_value = (*rgb, 1); bsdf.inputs["Roughness"].default_value = rough
    return f

TEX = os.environ.get("WL_TEXTURES", os.path.expanduser("~/.cache/wl-test/textures"))

def photo(base, scale, tint=(1, 1, 1), normal=1.0, sat=1.0, val=1.0):
    # Poly Haven CC0 scans (1k): colour, OpenGL normal and roughness, box-projected in object
    # space so every face gets undistorted texture without UVs.
    def f(nt, bsdf):
        tc = nt.nodes.new("ShaderNodeTexCoord")
        mp = nt.nodes.new("ShaderNodeMapping"); mp.inputs["Scale"].default_value = (scale, scale, scale)
        nt.links.new(tc.outputs["Object"], mp.inputs[0])
        def img(kind, non_color):
            t = nt.nodes.new("ShaderNodeTexImage")
            t.image = bpy.data.images.load(os.path.join(TEX, "%s_%s.jpg" % (base, kind)))
            if non_color: t.image.colorspace_settings.name = "Non-Color"
            t.projection = "BOX"; t.projection_blend = 0.25
            nt.links.new(mp.outputs[0], t.inputs[0])
            return t
        d = img("diff", False)
        hs = nt.nodes.new("ShaderNodeHueSaturation")
        hs.inputs["Saturation"].default_value = sat; hs.inputs["Value"].default_value = val
        nt.links.new(d.outputs[0], hs.inputs["Color"])
        tintn = nt.nodes.new("ShaderNodeMix"); tintn.data_type = "RGBA"; tintn.blend_type = "MULTIPLY"
        tintn.inputs["Factor"].default_value = 1.0; tintn.inputs[7].default_value = (*tint, 1)
        nt.links.new(hs.outputs[0], tintn.inputs[6])
        nt.links.new(grime(nt, tintn.outputs[2], 0.3, 3.0), bsdf.inputs["Base Color"])
        r = img("rough", True); nt.links.new(r.outputs[0], bsdf.inputs["Roughness"])
        n = img("nor", True)
        nm = nt.nodes.new("ShaderNodeNormalMap"); nm.inputs["Strength"].default_value = normal
        nt.links.new(n.outputs[0], nm.inputs["Color"]); nt.links.new(nm.outputs[0], bsdf.inputs["Normal"])
    return f

M_STONE = mat("stone", photo("castle_brick_07", 2.2, (1.0, 0.97, 0.9), 1.6, 0.12, 1.45))   # grey limestone blocks
M_DARKSTONE = mat("darkstone", flat((0.24, 0.21, 0.17)))
M_DIRT = mat("dirt", photo("dry_ground_01", 1.2, (0.9, 0.8, 0.65)))
M_COBBLE = mat("cobble", photo("cobblestone_floor_03", 2.5, (0.85, 0.8, 0.7), 1.5))
M_ROOF = mat("roof", photo("ceramic_roof_01", 1.6, (0.78, 0.66, 0.54), 2.0, 0.55, 0.9))   # brown-grey tiles
M_WOOD = mat("wood", photo("dark_planks", 2.0, (0.8, 0.65, 0.5)))
M_BEAM = mat("beam", flat((0.13, 0.07, 0.03)))
M_QUOIN = mat("quoin", photo("castle_brick_07", 3.0, (1.0, 0.98, 0.93), 1.0, 0.08, 1.7))
M_BUSH = mat("bush", flat((0.10, 0.20, 0.05)))
M_HOLE = mat("hole", flat((0.03, 0.025, 0.02)))
M_GOLD = mat("gold", flat((0.75, 0.52, 0.12), 0.35))
M_PLAYER = mat("player", flat((1.0, 0.0, 1.0)))   # magenta = player colour area

# ---------- geometry helpers ----------
def obj_from_bm(bm, name, m):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me); scn.collection.objects.link(o)
    o.data.materials.append(m)
    return o

def box(name, cx, cy, z0, sx, sy, sz, m):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((cx + v.co.x * sx, cy + v.co.y * sy, z0 + (v.co.z + 0.5) * sz))
    return obj_from_bm(bm, name, m)

def gable(name, cx, cy, z0, sx, sy, h, m, along="x"):
    # prism roof: ridge along x (or y), eaves at z0, ridge at z0+h
    bm = bmesh.new()
    if along == "x":
        pts = [(-sx/2, -sy/2, 0), (sx/2, -sy/2, 0), (sx/2, sy/2, 0), (-sx/2, sy/2, 0),
               (-sx/2, 0, h), (sx/2, 0, h)]
        faces = [(0, 1, 5, 4), (2, 3, 4, 5), (0, 4, 3), (1, 2, 5), (0, 3, 2, 1)]
    else:
        pts = [(-sx/2, -sy/2, 0), (sx/2, -sy/2, 0), (sx/2, sy/2, 0), (-sx/2, sy/2, 0),
               (0, -sy/2, h), (0, sy/2, h)]
        faces = [(1, 2, 5, 4), (3, 0, 4, 5), (0, 1, 4), (2, 3, 5), (0, 3, 2, 1)]
    vs = [bm.verts.new((cx + x, cy + y, z0 + z)) for x, y, z in pts]
    for f in faces: bm.faces.new([vs[i] for i in f])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return obj_from_bm(bm, name, m)

def pyramid(name, cx, cy, z0, s, h, m):
    bm = bmesh.new()
    b = [bm.verts.new((cx + x, cy + y, z0)) for x, y in ((-s/2, -s/2), (s/2, -s/2), (s/2, s/2), (-s/2, s/2))]
    t = bm.verts.new((cx, cy, z0 + h))
    for i in range(4): bm.faces.new((b[i], b[(i + 1) % 4], t))
    bm.faces.new(b[::-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return obj_from_bm(bm, name, m)

def cyl(name, cx, cy, z0, r, h, m, seg=24):
    bpy.ops.mesh.primitive_cylinder_add(vertices=seg, radius=r, depth=h, location=(cx, cy, z0 + h / 2))
    o = bpy.context.active_object; o.name = name; o.data.materials.append(m); return o

def cone(name, cx, cy, z0, r, h, m, seg=24):
    bpy.ops.mesh.primitive_cone_add(vertices=seg, radius1=r, depth=h, location=(cx, cy, z0 + h / 2))
    o = bpy.context.active_object; o.name = name; o.data.materials.append(m); return o

# ---------- the abbey (3x3 tile footprint: x, y in -1.5..1.5) ----------
# The camera looks from the -x/-y side, so the "front" walls face -x and -y.
# no ground slab: the game draws the terrain under the building
# nave: long hall along x
box("nave", 0.15, 0.25, 0.06, 2.2, 1.25, 0.95, M_STONE)
gable("nave_roof", 0.15, 0.25, 1.01, 2.36, 1.45, 0.62, M_ROOF, "x")
# transept / side chapel toward the viewer
box("chapel", -0.15, -0.75, 0.06, 0.9, 0.85, 0.75, M_STONE)
gable("chapel_roof", -0.15, -0.75, 0.81, 1.02, 1.0, 0.45, M_ROOF, "y")
# apse at the +x end
cyl("apse", 1.25, 0.25, 0.06, 0.48, 0.8, M_STONE)
cone("apse_roof", 1.25, 0.25, 0.86, 0.56, 0.42, M_ROOF)
# bell tower at the front-left corner
box("tower", -0.95, 0.55, 0.06, 0.72, 0.72, 1.95, M_STONE)
box("tower_band", -0.95, 0.55, 1.55, 0.78, 0.78, 0.07, M_DARKSTONE)
pyramid("tower_roof", -0.95, 0.55, 2.01, 0.86, 0.75, M_ROOF)
box("spire_gold", -0.95, 0.55, 2.74, 0.04, 0.04, 0.22, M_GOLD)
box("spire_bar", -0.95, 0.55, 2.86, 0.04, 0.16, 0.035, M_GOLD)
# buttresses on the nave's front (-y) wall
for x in (0.55, 1.05):
    box("buttress", x, -0.40, 0.06, 0.12, 0.14, 0.7, M_DARKSTONE)
# doors and windows (dark recesses), on walls facing the camera
box("door", -0.15, -1.18, 0.06, 0.32, 0.03, 0.42, M_WOOD)
box("door_arch", -0.15, -1.185, 0.48, 0.24, 0.03, 0.08, M_WOOD)
for x in (0.3, 0.8):
    box("win", x, -0.385, 0.45, 0.13, 0.03, 0.32, M_HOLE)
for y in (0.35, 0.75):
    box("win_t", -1.315, y, 1.15, 0.03, 0.12, 0.28, M_HOLE)
box("bell_open", -1.315, 0.55, 1.68, 0.03, 0.3, 0.26, M_HOLE)
box("bell_open2", -0.95, 0.185, 1.68, 0.3, 0.03, 0.26, M_HOLE)
box("rose", -0.6, 0.25, 1.15, 0.03, 0.25, 0.25, M_HOLE)
# player-colour banners hanging on the tower
box("banner1", -1.32, 0.55, 0.75, 0.02, 0.26, 0.55, M_PLAYER)
box("banner2", -0.95, 0.175, 0.75, 0.26, 0.02, 0.55, M_PLAYER)
# small garden wall and a well in front


# ---------- details: what makes the originals read as "real" at this size ----------
def quoins(cx, cy, sx, sy, z0, h, step=0.14):
    # alternating corner stones on the corners facing the camera
    for i, z in enumerate([z0 + k * step for k in range(int(h / step))]):
        w = 0.16 if i % 2 == 0 else 0.10
        for (qx, qy) in ((cx - sx / 2, cy - sy / 2), (cx - sx / 2, cy + sy / 2), (cx + sx / 2, cy - sy / 2)):
            box("quoin", qx, qy, z, w if i % 2 == 0 else 0.1, 0.1 if i % 2 == 0 else w, step * 0.92, M_QUOIN)
quoins(-0.95, 0.55, 0.72, 0.72, 0.06, 1.95)
quoins(-0.15, -0.75, 0.9, 0.85, 0.06, 0.75)
quoins(0.15, 0.25, 2.2, 1.25, 0.06, 0.95)
# eave beams and ridge caps
box("eave_n", 0.15, -0.37, 0.98, 2.24, 0.06, 0.05, M_BEAM)
box("eave_w", -0.6, -0.75, 0.78, 0.06, 0.88, 0.05, M_BEAM)
box("eave_e", 0.3, -0.75, 0.78, 0.06, 0.88, 0.05, M_BEAM)
M_RIDGE = mat("ridge", flat((0.22, 0.08, 0.04)))
box("ridge", 0.15, 0.25, 1.6, 2.38, 0.06, 0.05, M_RIDGE)
box("ridge_c", -0.15, -0.75, 1.23, 0.06, 1.02, 0.04, M_RIDGE)
# window and door surrounds (lighter stone)
for x in (0.3, 0.8):
    box("win_frame", x, -0.378, 0.42, 0.19, 0.02, 0.38, M_QUOIN)
box("door_frame", -0.15, -1.176, 0.06, 0.40, 0.02, 0.52, M_QUOIN)
# steps in front of the door, a cobbled path
box("step1", -0.15, -1.26, 0.03, 0.46, 0.14, 0.05, M_QUOIN)
box("step2", -0.15, -1.33, 0.03, 0.52, 0.12, 0.025, M_QUOIN)
box("path", -0.15, -1.42, 0.0, 0.42, 0.18, 0.03, M_COBBLE)
# ---------- props: Poly Haven CC0 models (glTF 1k), scaled from metres ----------
MODELS = os.environ.get("WL_MODELS", os.path.expanduser("~/.cache/wl-test/models"))
M_PER_UNIT = 0.22          # 1 m in the scene: a 4 m wall is about 0.9 units high

def prop(name, x, y, rot=0.0, height=0.2, material=None):
    """Import a model and scale it so it is `height` scene units tall, standing at (x, y)."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(MODELS, name, name + ".gltf"))
    new = [o for o in bpy.data.objects if o not in before]
    roots = [o for o in new if o.parent is None]
    bpy.context.view_layer.update()
    zs = [(o.matrix_world @ Vector(c)).z for o in new if o.type == "MESH" for c in o.bound_box]
    h0 = (max(zs) - min(zs)) if zs else 1.0
    k = height / h0 if h0 > 1e-6 else 1.0
    for o in roots:
        o.scale = tuple(v * k for v in o.scale)
        o.rotation_euler.z += math.radians(rot)
        o.location = (x, y, 0)
    bpy.context.view_layer.update()
    zs = [(o.matrix_world @ Vector(c)).z for o in new if o.type == "MESH" for c in o.bound_box]
    for o in roots: o.location.z -= min(zs)            # stand on the ground
    if material:
        for o in new:
            if o.type == "MESH":
                o.data.materials.clear(); o.data.materials.append(material)
    return new

prop("barrel_03", -1.3, -0.95, 20, 0.2, M_WOOD)
prop("barrel_03", -1.42, -1.12, 70, 0.2, M_WOOD)
prop("old_military_crate", -1.15, -1.18, 15, 0.12, M_WOOD)
prop("planter_pot_clay", -0.42, -1.26, 0, 0.1)
prop("planter_pot_clay", 0.12, -1.26, 0, 0.09)
prop("painted_wooden_bench", 0.55, -0.52, 0, 0.09, M_WOOD)
prop("rock_07", -1.38, -1.15, 40, 0.13)
prop("tree_stump_01", 1.38, 1.2, 0, 0.1)
# bushes: clusters of lumpy leaf balls (the scanned shrubs turn into sticks at this size)
def leaves(nt, bsdf):
    n = nt.nodes.new("ShaderNodeTexVoronoi"); n.inputs["Scale"].default_value = 60
    tc = nt.nodes.new("ShaderNodeTexCoord"); nt.links.new(tc.outputs["Object"], n.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (0.03, 0.07, 0.01, 1)
    ramp.color_ramp.elements[1].color = (0.20, 0.32, 0.06, 1)
    nt.links.new(n.outputs["Distance"], ramp.inputs[0]); nt.links.new(ramp.outputs[0], bsdf.inputs["Base Color"])
    bump(nt, bsdf, n.outputs["Distance"], 1.0)
    bsdf.inputs["Roughness"].default_value = 0.8
M_LEAF = mat("leaf", leaves)
import random
random.seed(11)
def bush(x, y, r):
    for i in range(6):
        a = random.random() * 6.28; d = r * 0.6 * random.random()
        rr = r * (0.55 + 0.35 * random.random())
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=rr,
            location=(x + d * math.cos(a), y + d * math.sin(a), rr * 0.8 + random.random() * r * 0.3))
        o = bpy.context.active_object; o.data.materials.append(M_LEAF)
        for v in o.data.vertices: v.co *= 0.85 + 0.3 * random.random()
bush(-1.38, -0.35, 0.12)
bush(-1.3, 1.25, 0.14)
bush(0.95, -1.3, 0.11)
bush(1.35, -0.95, 0.1)

# herb garden behind a wattle fence (right side), rows of small plants
M_FENCE = mat("fence", photo("dark_planks", 3.0, (0.7, 0.55, 0.4)))
# herb garden in front of the tower (lit side): fence on its front (-y) and left (-x) edges,
# x -1.42..-0.75, y -1.45..-1.25 is too thin, so the garden sits at x -1.42..-0.7, y -0.85..-0.45
for i in range(13):
    box("post", -1.42 + i * 0.06, -0.85, 0.0, 0.022, 0.022, 0.15, M_FENCE)
for i in range(7):
    box("post", -1.42, -0.85 + i * 0.065, 0.0, 0.022, 0.022, 0.15, M_FENCE)
for z in (0.055, 0.11):
    box("rail", -1.06, -0.85, z, 0.72, 0.015, 0.018, M_FENCE)
    box("rail", -1.42, -0.65, z, 0.015, 0.4, 0.018, M_FENCE)
box("bed", -1.06, -0.65, 0.0, 0.66, 0.36, 0.015, M_DIRT)
for row in range(3):
    for k in range(5):
        prop("shrub_sorrel_01", -1.33 + k * 0.13, -0.78 + row * 0.12, row * 40 + k * 25, 0.07)

# arched windows with dark coloured glass (replace the flat recesses)
M_GLASS = mat("glass", flat((0.05, 0.07, 0.14), 0.2))
def arch_window(x, y, z, w, h, normal_axis):
    if normal_axis == "y":
        box("frame", x, y + 0.006, z - 0.02, w + 0.06, 0.02, h + 0.06, M_QUOIN)
        box("glass", x, y, z, w, 0.03, h, M_GLASS)
        bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=w / 2, depth=0.03, location=(x, y, z + h), rotation=(math.pi / 2, 0, 0))
    else:
        box("frame", x + 0.006, y, z - 0.02, 0.02, w + 0.06, h + 0.06, M_QUOIN)
        box("glass", x, y, z, 0.03, w, h, M_GLASS)
        bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=w / 2, depth=0.03, location=(x, y, z + h), rotation=(0, math.pi / 2, 0))
    bpy.context.active_object.data.materials.append(M_GLASS)
for o in [o for o in bpy.data.objects if o.name.startswith(("win", "win_t", "win_frame"))]:
    bpy.data.objects.remove(o)
for x in (0.3, 0.8):
    arch_window(x, -0.39, 0.42, 0.12, 0.26, "y")
for y in (0.35, 0.75):
    arch_window(-1.32, y, 1.1, 0.11, 0.24, "x")
arch_window(-0.6, 0.25, 1.12, 0.2, 0.06, "x")   # rose window over the transept

# crenellated tower top (merlons around the roof base)
for i in range(5):
    t = -0.36 + i * 0.18
    for (mx, my) in ((-0.95 + t, 0.19), (-0.95 + t, 0.91), (-1.31, 0.55 + t), (-0.59, 0.55 + t)):
        box("merlon", mx, my, 2.01, 0.09, 0.09, 0.12, M_QUOIN)
# irregular soil patch under the building (the originals stand on worn earth)
M_SOIL = mat("soil", photo("dry_ground_01", 1.2, (1.0, 0.72, 0.45), 1.0, 0.9, 1.0))
bm = bmesh.new(); ring = []
random.seed(3)
for k in range(48):
    a = k / 48 * 2 * math.pi
    rr = 1.75 * (0.9 + 0.12 * random.random())
    x, y = rr * math.cos(a), rr * math.sin(a)
    m = max(abs(x), abs(y)) / 1.55                     # squarish: follow the 3x3 footprint
    ring.append(bm.verts.new((x / max(m, 1.0) * 1.0, y / max(m, 1.0) * 1.0, 0.004)))
bm.faces.new(ring)
obj_from_bm(bm, "soil", M_SOIL)
# ivy on the lit walls
M_IVY = mat("ivy", leaves)
for (ix, iy, h) in ((-1.33, 0.25, 0.5), (-1.33, 0.95, 0.35), (-0.62, -1.18, 0.3), (0.6, -0.39, 0.28)):
    for k in range(10):
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.035 + 0.02 * random.random(),
            location=(ix + (random.random() - 0.5) * 0.12, iy + (random.random() - 0.5) * 0.12, random.random() * h))
        bpy.context.active_object.data.materials.append(M_IVY)

# ---------- ground (shadow catcher) ----------
bpy.ops.mesh.primitive_plane_add(size=12, location=(0, 0, 0))
ground = bpy.context.active_object; ground.name = "ground"; ground.is_shadow_catcher = True

# ---------- light ----------
sun = bpy.data.lights.new("sun", "SUN"); sun.energy = 7.5; sun.angle = math.radians(2)
sun.color = (1.0, 0.9, 0.75)
so = bpy.data.objects.new("sun", sun); scn.collection.objects.link(so)
so.rotation_euler = (math.radians(50), 0, math.radians(58))   # sun from screen-right like the game (palm shadows fall to the lower left)
w = bpy.data.worlds.new("w"); scn.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.45, 0.5, 0.62, 1)
w.node_tree.nodes["Background"].inputs[1].default_value = 0.3

# ---------- camera: AoE2 2:1 dimetric ----------
cam = bpy.data.cameras.new("cam"); cam.type = "ORTHO"
co = bpy.data.objects.new("cam", cam); scn.collection.objects.link(co); scn.camera = co
elev = math.atan(0.5)                          # 26.565 deg: tiles become 2:1 diamonds
co.rotation_euler = (math.pi / 2 - elev, 0, math.radians(-45))
fwd = co.rotation_euler.to_matrix() @ Vector((0, 0, -1))
co.location = -fwd * 30 + Vector((0, 0, 0.9))
px_per_unit = TILE_PX / math.sqrt(2)           # a 1x1 tile's diagonal is 96 px
cam.sensor_fit = "HORIZONTAL"          # ortho_scale is the image WIDTH (AUTO used the taller side: 12% too big)
cam.ortho_scale = W / px_per_unit

scn.render.engine = "CYCLES"; scn.cycles.samples = 64; scn.cycles.use_denoising = True
scn.render.resolution_x = W * SS; scn.render.resolution_y = H * SS; scn.render.resolution_percentage = 100
scn.render.film_transparent = True; scn.render.filter_size = 0.75
scn.view_settings.view_transform = "Standard"
scn.render.image_settings.file_format = "PNG"; scn.render.image_settings.color_mode = "RGBA"

# hotspot = where the ground origin lands in the image
bpy.context.view_layer.update()        # camera matrix must be current, or the hotspot is the image centre
from bpy_extras.object_utils import world_to_camera_view
v = world_to_camera_view(scn, co, Vector((0, 0, 0)))
hx, hy = round(v.x * W), round((1 - v.y) * H)

scn.render.filepath = os.path.join(OUT, "abbey_full.png"); bpy.ops.render.render(write_still=True)
ground.hide_render = True
scn.render.filepath = os.path.join(OUT, "abbey_color.png"); bpy.ops.render.render(write_still=True)
# normals pass for the contour lines (to_palette.py): every surface coloured by its direction,
# no lighting, so lines appear only where the geometry turns or ends (not on texture detail)
nm = bpy.data.materials.new("normals"); nm.use_nodes = True; nt = nm.node_tree; nt.nodes.clear()
geo = nt.nodes.new("ShaderNodeNewGeometry"); vm = nt.nodes.new("ShaderNodeVectorMath"); vm.operation = "MULTIPLY_ADD"
vm.inputs[1].default_value = (0.5, 0.5, 0.5); vm.inputs[2].default_value = (0.5, 0.5, 0.5)
em = nt.nodes.new("ShaderNodeEmission"); out = nt.nodes.new("ShaderNodeOutputMaterial")
nt.links.new(geo.outputs["Normal"], vm.inputs[0]); nt.links.new(vm.outputs[0], em.inputs[0]); nt.links.new(em.outputs[0], out.inputs[0])
scn.view_layers[0].material_override = nm
scn.cycles.samples = 8; scn.cycles.use_denoising = False
scn.render.filepath = os.path.join(OUT, "abbey_normal.png"); bpy.ops.render.render(write_still=True)
open(os.path.join(OUT, "abbey_info.txt"), "w").write("%d %d %d %d %d\n" % (W, H, hx, hy, SS))
print("hotspot", hx, hy)
