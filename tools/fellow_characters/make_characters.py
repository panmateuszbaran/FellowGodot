#!/usr/bin/env python3
"""FellowEngine's character pipeline (milestone 232): people made with MakeHuman's MPFB 2 in Blender, exported as glTF.

Runs in Python with Blender as a module (`pip install bpy`, Blender 5.x; Pillow) and MPFB 2 from its source
(https://github.com/makehumancommunity/mpfb2, GPLv3: it stays outside this repository, installed into a Blender profile
of the tool's own). Its assets are MakeHuman's system assets pack (CC0: the base mesh, its targets, skins, eyes, hair,
clothes; https://static.makehumancommunity.org/assets/assetpacks/makehuman_system_assets.html), so the characters it
writes are CC0 too.

    python3 tools/fellow_characters/make_characters.py --mpfb <mpfb2>/src/mpfb --assets <unpacked system assets> \
        --out showcase/people [--spec tools/fellow_characters/people.json] [--only name,...] [--profile <dir>]

Each character of the spec: the body's macros (gender 0 female - 1 male, age 0.5 = 25 years - 1 = 90, muscle, weight,
height, proportions, the three ethnic mixes), a skin, eyes, eyebrows, eyelashes, hair and clothes from the pack, the
"game_engine" rig (53 bones, Unreal Engine's mannequin's names: Godot's bone mapper maps them to its humanoid profile)
and the faces hidden under the clothes removed; the bones renamed to Godot's humanoid profile, the materials opaque but
for hair, eyebrows and eyelashes, the textures at most 2048 (the skin) and 1024 px. One <name>.glb each, its textures
in it.
"""

import argparse
import importlib
import json
import os
import sys
import tempfile

# MPFB 2's package folder (set from --mpfb: its data/targets has the face modifiers' targets).
MPFB_DIR = ""

# Milestone 243: the pack's high-poly eyes (1 064 vertices, a round eyeball and cornea; 232-242: low-poly, 96).
EYES_MESH = "high-poly"

# Milestone 249: the skin's ambient occlusion baked per person (Cycles, in the body's UV layout: <name>_ao.png) and a
# second UV map for the skin's micro-relief (the first's islands scaled to one density: DETAIL_TILE metres of skin per
# repeat of make_skin_maps.py's skin_pores_normal.png).
SKIN_AO_SIZE = 1024
SKIN_AO_SAMPLES = 64
SKIN_AO_DISTANCE = 0.06  # m: crevices' scale (eye sockets, nostrils, the mouth's corners, ears, fingers)
DETAIL_TILE = 0.02  # m
SKIN_AO = True


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--mpfb", required=True, help="MPFB 2's package folder (mpfb2/src/mpfb)")
    ap.add_argument("--assets", required=True, help="MakeHuman's system assets, unpacked (skins/, eyes/, hair/, clothes/ ...)")
    ap.add_argument("--out", required=True, help="where the .glb files go")
    ap.add_argument("--spec", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "people.json"))
    ap.add_argument("--only", default="", help="comma-separated names of the spec's characters")
    ap.add_argument("--profile", default="", help="a Blender user folder for MPFB (default: a new temporary one)")
    ap.add_argument("--blend", action="store_true", help="also save each character's .blend (to look at)")
    ap.add_argument("--hair_dump", default="", help="milestone 244: also save the hair's traced data (.npz) here, to try "
                    "hair_guides.py alone; 'skip': no strand guides")
    ap.add_argument("--ao", default="1", help="milestone 249: 0 to skip baking the skin's ambient occlusion")
    ap.add_argument("--faces", default="", help="MakeHuman's face units (the Faceunits 01 pack's targets/faceunits, CC0: "
                    "ARKit's 52 shapes); default <assets>/targets/faceunits if there; 'none': no face shapes")
    return ap.parse_args()


def setup_mpfb(args):
    global MPFB_DIR
    MPFB_DIR = os.path.abspath(args.mpfb)
    # Blender's user folder (its extensions, MPFB's settings and data) before bpy is imported.
    profile = os.path.abspath(args.profile) if args.profile else tempfile.mkdtemp(prefix="fellow_mpfb_")
    os.environ["BLENDER_USER_RESOURCES"] = profile
    import bpy  # noqa: E402  (Blender as a module)
    import addon_utils  # noqa: E402

    ext = bpy.utils.user_resource("EXTENSIONS", path="user_default", create=True)
    link = os.path.join(ext, "mpfb")
    if not os.path.exists(link):
        os.symlink(os.path.abspath(args.mpfb), link)
    # MPFB looks for assets in its user data folder.
    data = os.path.join(ext, ".user", "user_default", "mpfb", "data")
    os.makedirs(data, exist_ok=True)
    for sub in os.listdir(args.assets):
        src = os.path.join(os.path.abspath(args.assets), sub)
        dst = os.path.join(data, sub)
        if os.path.isdir(src) and not os.path.exists(dst):
            os.symlink(src, dst)
    errors = []
    addon_utils.enable("bl_ext.user_default.mpfb", default_set=True, handle_error=errors.append)
    if errors:
        raise RuntimeError("MPFB did not load: %s" % errors)

    def service(name, cls):
        return getattr(importlib.import_module("bl_ext.user_default.mpfb." + name), cls)

    return bpy, service


# MPFB's "game_engine" rig (Unreal Engine's mannequin's names) to Godot's SkeletonProfileHumanoid: the characters' bones
# carry the profile's names, so a RetargetModifier3D (or an AnimationPlayer's humanoid tracks) drives them as it is.
HUMANOID = {"pelvis": "Hips", "spine_01": "Spine", "spine_02": "Chest", "spine_03": "UpperChest", "neck_01": "Neck",
            "head": "Head"}
for _s, _side in (("_l", "Left"), ("_r", "Right")):
    HUMANOID.update({"clavicle" + _s: _side + "Shoulder", "upperarm" + _s: _side + "UpperArm",
                     "lowerarm" + _s: _side + "LowerArm", "hand" + _s: _side + "Hand",
                     "thumb_01" + _s: _side + "ThumbMetacarpal", "thumb_02" + _s: _side + "ThumbProximal",
                     "thumb_03" + _s: _side + "ThumbDistal", "thigh" + _s: _side + "UpperLeg",
                     "calf" + _s: _side + "LowerLeg", "foot" + _s: _side + "Foot", "ball" + _s: _side + "Toes"})
    for _f, _g in (("index", "Index"), ("middle", "Middle"), ("ring", "Ring"), ("pinky", "Little")):
        for _n, _k in (("01", "Proximal"), ("02", "Intermediate"), ("03", "Distal")):
            HUMANOID["%s_%s%s" % (_f, _n, _s)] = _side + _g + _k

# Texture sizes kept (the longest side): the body's skin, the rest.
BODY_TEXTURE = 2048
OTHER_TEXTURE = 1024
def game_materials(bpy, root, cut_names):
    """The export's materials for a game: opaque unless they are hair, eyebrows or eyelashes (alpha unlinked, so the
    exporter writes them opaque and as JPEG), textures no larger than BODY_TEXTURE / OTHER_TEXTURE."""
    done = set()
    for ob in [root] + list(root.children_recursive):
        if ob.type != "MESH":
            continue
        for slot in ob.material_slots:
            m = slot.material
            if m is None or m.name in done or m.node_tree is None:
                continue
            done.add(m.name)
            cut = any(k in m.name for k in cut_names)
            for link in list(m.node_tree.links):
                if link.to_socket.name == "Alpha" and not cut:
                    m.node_tree.links.remove(link)
            for n in m.node_tree.nodes:
                if n.type == "TEX_IMAGE" and n.image is not None:
                    limit = BODY_TEXTURE if m.name.endswith(".body") else OTHER_TEXTURE
                    w, h = n.image.size
                    if max(w, h) > limit:
                        n.image.scale(max(1, w * limit // max(w, h)), max(1, h * limit // max(w, h)))


def repack_images(path, quality=88):
    """Blender writes its JPEGs with an ICC segment Godot's JPEG loader refuses: every image of the .glb encoded again
    with Pillow (JPEG without metadata, PNG with a palette), the binary chunk rebuilt."""
    import io
    import struct

    from PIL import Image

    with open(path, "rb") as f:
        data = f.read()
    json_len = struct.unpack("<I", data[12:16])[0]
    gltf = json.loads(data[20 : 20 + json_len])
    bin_start = 20 + json_len + 8
    blob = data[bin_start:]
    views = gltf["bufferViews"]
    image_views = {im["bufferView"]: im for im in gltf.get("images", []) if "bufferView" in im}
    out = bytearray()
    for i, view in enumerate(views):
        chunk = blob[view.get("byteOffset", 0) : view.get("byteOffset", 0) + view["byteLength"]]
        if i in image_views:
            im = Image.open(io.BytesIO(chunk))
            buf = io.BytesIO()
            if image_views[i].get("mimeType") == "image/jpeg":
                im.convert("RGB").save(buf, "JPEG", quality=quality, optimize=True)
            elif image_views[i].get("name", "").endswith("_eye"):
                # (milestone 243: the high-poly eyes' texture — its cornea's island transparent — in full colour: a
                # palette bands the iris)
                im.convert("RGBA").save(buf, "PNG", optimize=True)
            else:
                # (the hair's, eyebrows' and eyelashes' cards: their alpha only cuts — a palette of 256 colours and
                # alphas keeps them, at an eighth of the size)
                im.convert("RGBA").quantize(256, method=Image.Quantize.FASTOCTREE).save(buf, "PNG", optimize=True)
            chunk = buf.getvalue()
        while len(out) % 4:
            out.append(0)
        view["byteOffset"] = len(out)
        view["byteLength"] = len(chunk)
        out += chunk
    while len(out) % 4:
        out.append(0)
    gltf["buffers"][0]["byteLength"] = len(out)
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    js += b" " * ((4 - len(js) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(out)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(out), 0x004E4942) + bytes(out))


def apply_shape_keys(bpy, root, keep):
    """MPFB keeps the body's macros (gender, age, weight...) as shape keys over the neutral base mesh; the exporter
    would write the base mesh. Their mix applied to the mesh; the face's shapes (`keep`) kept as shape keys over it,
    their offsets as they were."""
    import numpy as np

    for ob in [root] + list(root.children_recursive):
        if ob.type != "MESH" or ob.data.shape_keys is None:
            continue
        blocks = ob.data.shape_keys.key_blocks
        n = len(ob.data.vertices)

        def coords(block):
            a = np.empty(n * 3, dtype=np.float32)
            block.data.foreach_get("co", a)
            return a

        offsets = {}
        for kb in blocks:
            if kb.name in keep:
                offsets[kb.name] = coords(kb) - coords(kb.relative_key)
                kb.value = 0.0
        bpy.ops.object.select_all(action="DESELECT")
        ob.select_set(True)
        bpy.context.view_layer.objects.active = ob
        bpy.ops.object.shape_key_remove(all=True, apply_mix=True)
        if offsets:
            shaped = np.empty(n * 3, dtype=np.float32)
            ob.data.vertices.foreach_get("co", shaped)
            ob.shape_key_add(name="Basis", from_mix=False)
            for name in sorted(offsets):
                kb = ob.shape_key_add(name=name, from_mix=False)
                kb.data.foreach_set("co", shaped + offsets[name])
                kb.value = 0.0


def face_units(bpy, service, body, face_dir, followers):
    """ARKit's 52 face shapes (MakeHuman's Faceunits 01, CC0) loaded on the body as shape keys at 0; the eyebrows,
    eyelashes, teeth and tongue given the same shapes (each fitted again to the body with one shape at 1: they follow
    the lids, the brows and the jaw). Returns the shapes' names."""
    import numpy as np

    TargetService = service("services.targetservice", "TargetService")
    ClothesService = service("services.clothesservice", "ClothesService")
    names = sorted(f[: -len(".target")] for f in os.listdir(face_dir) if f.endswith(".target"))
    for name in names:
        TargetService.load_target(body, os.path.join(face_dir, name + ".target"), weight=0.0, name=name)
    blocks = body.data.shape_keys.key_blocks
    for ob, prefixes in followers:
        n = len(ob.data.vertices)
        shapes = {}
        for name in names:
            if not name.startswith(prefixes):
                continue
            blocks[name].value = 1.0
            ClothesService.fit_clothes_to_human(ob, body)
            a = np.empty(n * 3, dtype=np.float32)
            ob.data.vertices.foreach_get("co", a)
            shapes[name] = a
            blocks[name].value = 0.0
        ClothesService.fit_clothes_to_human(ob, body)
        if shapes:
            ob.shape_key_add(name="Basis", from_mix=False)
            for name in sorted(shapes):
                kb = ob.shape_key_add(name=name, from_mix=False)
                kb.data.foreach_set("co", shapes[name])
    return names


HATS = ("fedora", "hat", "cap", "helmet", "hood")
HAT_REACH = 0.02 # m
HAT_MARGIN = 0.005 # m


def trim_under_hat(bpy, root, hair_name, clothes):
    """Milestone 236: hair under a hat — MPFB fits the hair to the head, not to the hat, and its cards came through the
    hat's crown (black patches on Aldo's fedora). The hair's faces with a vertex above the hat's lowest point over it
    (within HAT_REACH seen from above, less HAT_MARGIN) are removed: what hangs below the brim stays."""
    import bmesh

    hats = [c for c in clothes if any(h in c.lower() for h in HATS)]
    if not hair_name or not hats:
        return 0
    meshes = [c for c in root.children_recursive if c.type == "MESH"]
    hair = next((c for c in meshes if hair_name in c.name), None)
    hat = next((c for c in meshes if any(h in c.name for h in hats)), None)
    if hair is None or hat is None:
        return 0
    from mathutils import Vector, kdtree

    pts = [hat.matrix_world @ v.co for v in hat.data.vertices]
    tree = kdtree.KDTree(len(pts))
    for i, p in enumerate(pts):
        tree.insert(Vector((p.x, p.y, 0.0)), i)
    tree.balance()
    bm = bmesh.new()
    bm.from_mesh(hair.data)
    mw = hair.matrix_world
    inside = set()
    for v in bm.verts:
        w = mw @ v.co
        # (the hat's lowest point over this one, within 2 cm seen from above: the brim's underside or the crown's rim)
        near = tree.find_range(Vector((w.x, w.y, 0.0)), HAT_REACH)
        if near and w.z > min(pts[i].z for _, i, _ in near) - HAT_MARGIN:
            inside.add(v.index)
    gone = [f for f in bm.faces if any(v.index in inside for v in f.verts)]
    bmesh.ops.delete(bm, geom=gone, context="FACES")
    loose = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=loose, context="VERTS")
    bm.to_mesh(hair.data)
    bm.free()
    hair.data.update()
    return len(gone)


def eye_colour(bpy, eyes, find_eye, colour):
    """Milestone 235: the irises' colour (the spec's `eye_colour`: one of the pack's eye textures, eyes/materials/
    <colour>_eye.png; or "darkbrown": the pack's brown — a red-brown — turned towards a neutral dark brown)."""
    from PIL import Image

    src = find_eye(("brown" if colour == "darkbrown" else colour) + "_eye")
    if colour == "darkbrown":
        rgba = Image.open(src).convert("RGBA")
        im = rgba.convert("RGB")
        h, sat, v = im.convert("HSV").split()
        # (the iris's hue, ~10 degrees, to 25; its saturation two thirds; the white and the skin's tones barely move)
        h = h.point(lambda x: x + int(10 * (1.0 - abs(x - 8) / 40.0)) if x < 40 else x)
        sat = sat.point(lambda x: int(x * 0.68))
        dst = os.path.join(tempfile.mkdtemp(prefix="fellow_eyes_"), "darkbrown_eye.png")
        out = Image.merge("HSV", (h, sat, v)).convert("RGB")
        out.putalpha(rgba.getchannel("A"))  # (the high-poly eyes' cornea is transparent: milestone 243)
        out.save(dst)
        src = dst
    image = bpy.data.images.load(src)
    for slot in eyes.material_slots:
        if slot.material and slot.material.node_tree:
            for n in slot.material.node_tree.nodes:
                if n.type == "TEX_IMAGE" and n.image is not None and n.image.filepath.endswith("_eye.png"):
                    n.image = image


def eye_bones(bpy, root, eyes_name):
    """Milestone 235: Godot's humanoid LeftEye and RightEye bones under the head, at the eyeballs' centres (MPFB's game
    rig has none), the eyes' mesh weighted to them (each eyeball wholly to its own): the eyes can turn."""
    from mathutils import Vector

    arm = root if root.type == "ARMATURE" else next((c for c in root.children_recursive if c.type == "ARMATURE"), None)
    eyes = next((c for c in root.children_recursive if c.type == "MESH" and eyes_name in c.name), None)
    if arm is None or eyes is None or "Head" not in arm.data.bones:
        return
    world = [eyes.matrix_world @ v.co for v in eyes.data.vertices]
    sides = {"LeftEye": [w for w in world if w.x > 0.0], "RightEye": [w for w in world if w.x <= 0.0]}
    if not sides["LeftEye"] or not sides["RightEye"]:
        return
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    to_arm = arm.matrix_world.inverted()
    for name, pts in sides.items():
        lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
        hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
        centre = to_arm @ ((lo + hi) * 0.5)
        bone = arm.data.edit_bones.new(name)
        bone.head = centre
        bone.tail = centre + Vector((0.0, -0.02, 0.0))  # (forward: the people face -Y)
        bone.parent = arm.data.edit_bones["Head"]
        bone.use_deform = True
    bpy.ops.object.mode_set(mode="OBJECT")
    for g in list(eyes.vertex_groups):
        eyes.vertex_groups.remove(g)
    groups = {n: eyes.vertex_groups.new(name=n) for n in sides}
    for v, w in zip(eyes.data.vertices, world):
        groups["LeftEye" if w.x > 0.0 else "RightEye"].add([v.index], 1.0, "REPLACE")


def face_shape(service, body, mpfb_dir, modifiers, asym_seed=None, asym=0.0):
    """Milestone 245: the face's own shape — MakeHuman's face modifiers (the spec's `face_shape`: a modifier's label
    from MPFB's data/targets/target.json, e.g. "nose-hump-decr-incr", to a value -1..1; a single target such as
    "head-oval" 0..1) loaded as the body's shape keys (applied with the macros at the export), both sides of a sided
    one; and a little asymmetry (no face is symmetric): every "asym-*" modifier of the face at a random value in
    ±`asym`. Before the assets are fitted, so that the eyebrows, eyes and clothes follow."""
    import random

    TargetService = service("services.targetservice", "TargetService")
    tdir = os.path.join(mpfb_dir, "data", "targets")
    with open(os.path.join(tdir, "target.json"), encoding="utf-8") as f:
        meta = json.load(f)
    by_label = {}
    for cat, info in meta.items():
        for c in info.get("categories", []):
            by_label[c["label"]] = (cat, c)
    mods = dict(modifiers or {})
    if asym > 0.0:
        rng = random.Random(asym_seed)
        for label in sorted(by_label):
            if label.startswith("asym-") and not any(k in label for k in ("breast", "trunk")):
                mods.setdefault(label, rng.uniform(-asym, asym))
    loaded = 0
    for label, value in sorted(mods.items()):
        if label not in by_label or abs(value) < 1e-4:
            if label not in by_label:
                print("make_characters: no face modifier %r" % label)
            continue
        cat, c = by_label[label]
        names = []
        if "opposites" in c:
            sign = "positive-" if value > 0 else "negative-"
            sides = ("left", "right") if c.get("has_left_and_right") else ("unsided",)
            names = [c["opposites"].get(sign + side, "") for side in sides]
        else:
            names = c.get("targets", [])[:1] if value > 0 else []
        for name in [n for n in names if n]:
            path = os.path.join(tdir, cat, name + ".target.gz")
            if os.path.exists(path):
                TargetService.load_target(body, path, weight=min(abs(float(value)), 1.0), name="face-" + name)
                loaded += 1
    return loaded


def hair_strands(bpy, root, body, hair_name, path_json, dump=""):
    """Milestone 244: the person's hair traced into strand guides (hair_guides.py) for FellowHair3D, written to
    <name>_hair.json beside the .glb; `dump`: the traced data also saved (.npz) to try the tracer on alone."""
    import numpy as np
    from PIL import Image

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import hair_guides

    hair = next((c for c in root.children_recursive if c.type == "MESH" and hair_name and hair_name in c.name), None)
    arm = root if root.type == "ARMATURE" else next((c for c in root.children_recursive if c.type == "ARMATURE"), None)
    if hair is None or arm is None or "Head" not in arm.data.bones:
        return 0
    image = None
    for slot in hair.material_slots:
        if slot.material and slot.material.node_tree:
            for n in slot.material.node_tree.nodes:
                if n.type == "TEX_IMAGE" and n.image is not None:
                    image = n.image
    if image is None:
        return 0
    rgba = np.asarray(Image.open(bpy.path.abspath(image.filepath)).convert("RGBA")).astype(np.float32) / 255.0
    me = hair.data
    me.calc_loop_triangles()
    uv = me.uv_layers.active.data
    mw = hair.matrix_world
    co = [mw @ v.co for v in me.vertices]
    tris = np.array([[list(co[i]) for i in t.vertices] for t in me.loop_triangles])
    uvs = np.array([[list(uv[l].uv) for l in t.loops] for t in me.loop_triangles])
    bw = body.matrix_world
    bn = bw.to_3x3().inverted().transposed()
    bverts = np.array([list(bw @ v.co) for v in body.data.vertices])
    bnorm = np.array([list((bn @ v.normal).normalized()) for v in body.data.vertices])
    head = arm.data.bones["Head"]
    hw = arm.matrix_world
    h0, h1 = hw @ head.head_local, hw @ head.tail_local
    axis = (h1 - h0).normalized()
    centre = np.array(list(h0 + axis * 0.085))
    radius = 0.085
    if dump:
        os.makedirs(dump, exist_ok=True)
        np.savez_compressed(os.path.join(dump, os.path.basename(path_json).replace(".json", ".npz")), tris=tris, uvs=uvs,
                            rgba=rgba, body_verts=bverts, body_normals=bnorm, head_center=centre, head_radius=radius)
    guides, root_c, tip_c = hair_guides.trace(tris, uvs, rgba, bverts, bnorm, centre, radius)
    hair_guides.write_json(path_json, guides, root_c, tip_c, centre, radius, True)
    return len(guides)


def humanoid_bones(root):
    arm = root if root.type == "ARMATURE" else next((c for c in root.children_recursive if c.type == "ARMATURE"), None)
    if arm is None:
        raise RuntimeError("no armature in the export copy")
    for bone in arm.data.bones:
        if bone.name in HUMANOID:
            bone.name = HUMANOID[bone.name]  # (Blender renames the meshes' vertex groups with it)


def find(asset_service, name, subdir, ext):
    path = asset_service.find_asset_absolute_path(name + ext, asset_subdir=subdir)
    if path is None:
        raise FileNotFoundError("%s/%s%s not in the assets" % (subdir, name, ext))
    return path


def skin_ao(bpy, root, body, path, cards, size=SKIN_AO_SIZE, samples=SKIN_AO_SAMPLES, distance=SKIN_AO_DISTANCE):
    """Milestone 249: the body's ambient occlusion baked into its UV layout with Cycles — the crevices a screen-space
    effect misses at a person's scale: the eye sockets round the eyeballs, the nostrils, the mouth's corners and inside,
    behind and inside the ears, between the fingers and toes, the clothes' edges on the skin. The eyes, teeth, tongue
    and clothes occlude; the hair's, eyebrows' and eyelashes' cards (their alpha cut) do not. Written as a grey PNG."""
    import numpy as np
    from PIL import Image

    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = samples
    if scene.world is None:
        scene.world = bpy.data.worlds.new("ao_world")
    scene.world.light_settings.distance = distance
    hidden = []
    mine = set(root.children_recursive) | {root}
    for ob in bpy.context.scene.objects:
        # (the character the export copy was made from stands in the same place)
        if ob.type == "MESH" and ob not in mine and not ob.hide_render:
            ob.hide_render = True
            hidden.append(ob)
    for ob in root.children_recursive:
        if ob.type == "MESH" and ob != body and not ob.hide_render:
            names = (ob.name + " " + " ".join(s.material.name for s in ob.material_slots if s.material)).lower()
            if any(k.lower() in names for k in cards):
                ob.hide_render = True
                hidden.append(ob)
    mat = body.active_material
    img = bpy.data.images.new("skin_ao_bake", size, size, alpha=False, float_buffer=True)
    # (texels no face covers keep 2.0: afterwards each takes its nearest baked texel's value, so that the texture's
    # mipmaps do not darken the islands' edges)
    img.pixels.foreach_set(np.full(size * size * 4, 2.0, dtype=np.float32))
    node = mat.node_tree.nodes.new("ShaderNodeTexImage")
    node.image = img
    for n in mat.node_tree.nodes:
        n.select = False
    node.select = True
    mat.node_tree.nodes.active = node
    bpy.ops.object.select_all(action="DESELECT")
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    body.data.uv_layers.active_index = 0
    bpy.ops.object.bake(type="AO", margin=2, use_clear=False)
    buf = np.empty(size * size * 4, dtype=np.float32)
    img.pixels.foreach_get(buf)
    px = buf.reshape(size, size, 4)[::-1, :, 0].copy()
    empty = px > 1.5
    if empty.any() and not empty.all():
        from scipy.ndimage import distance_transform_edt

        idx = distance_transform_edt(empty, return_distances=False, return_indices=True)
        px = px[idx[0], idx[1]]
    Image.fromarray((np.clip(px, 0.0, 1.0) * 255 + 0.5).astype(np.uint8), "L").save(path, optimize=True)
    mat.node_tree.nodes.remove(node)
    bpy.data.images.remove(img)
    for ob in hidden:
        ob.hide_render = False
    return float(px[~empty].mean()) if (~empty).any() else 0.0


def detail_uv(bpy, body, tile=DETAIL_TILE):
    """Milestone 249: a second UV map for the skin's micro-relief: the first's islands each scaled (about the origin,
    so its tangent frame stays the first's) to one density, `tile` metres of skin per unit — the pores the same size on
    the nose as on the back, whatever room each island has in the texture."""
    import bmesh

    me = body.data
    bm = bmesh.new()
    bm.from_mesh(me)
    uv = bm.loops.layers.uv[0]
    parent = list(range(len(bm.faces)))

    def root_of(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    bm.faces.ensure_lookup_table()
    for e in bm.edges:
        if len(e.link_faces) != 2:
            continue
        f0, f1 = e.link_faces
        same = True
        for v in e.verts:
            u0 = next(l[uv].uv for l in f0.loops if l.vert == v)
            u1 = next(l[uv].uv for l in f1.loops if l.vert == v)
            if (u0 - u1).length > 1e-5:
                same = False
                break
        if same:
            a, b = root_of(f0.index), root_of(f1.index)
            if a != b:
                parent[a] = b
    area3, areauv = {}, {}
    for f in bm.faces:
        r = root_of(f.index)
        area3[r] = area3.get(r, 0.0) + f.calc_area()
        co = [l[uv].uv for l in f.loops]
        a = 0.0
        for k in range(1, len(co) - 1):
            a += abs((co[k] - co[0]).cross(co[k + 1] - co[0])) * 0.5
        areauv[r] = areauv.get(r, 0.0) + a
    scale = {r: (area3[r] / areauv[r]) ** 0.5 / tile if areauv[r] > 1e-12 else 0.0 for r in area3}
    uv2 = bm.loops.layers.uv.new("detail")
    for f in bm.faces:
        s = scale[root_of(f.index)]
        for l in f.loops:
            l[uv2].uv = l[uv].uv * s
    bm.to_mesh(me)
    bm.free()
    me.uv_layers.active_index = 0
    for i, layer in enumerate(me.uv_layers):
        layer.active_render = i == 0
    return len(scale)


def make(bpy, service, spec, out, save_blend, face_dir, hair_dump=""):
    HumanService = service("services.humanservice", "HumanService")
    TargetService = service("services.targetservice", "TargetService")
    AssetService = service("services.assetservice", "AssetService")
    ExportService = service("services.exportservice", "ExportService")
    ObjectService = service("services.objectservice", "ObjectService")
    HumanObjectProperties = service("entities.objectproperties", "HumanObjectProperties")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    body = HumanService.create_human()
    for macro in ("gender", "age", "muscle", "weight", "height", "proportions", "african", "asian", "caucasian", "cupsize", "firmness"):
        if macro in spec:
            HumanObjectProperties.set_value(macro, float(spec[macro]), entity_reference=body)
    TargetService.reapply_macro_details(body)
    if spec.get("face_shape") or spec.get("face_asym", 0.0):
        n = face_shape(service, body, MPFB_DIR, spec.get("face_shape"), spec["name"], float(spec.get("face_asym", 0.0)))
        print("make_characters: %s's face: %d targets" % (spec["name"], n))

    HumanService.set_character_skin(find(AssetService, spec["skin"], "skins", ".mhmat"), body, skin_type="GAMEENGINE")
    HumanService.add_builtin_rig(body, "game_engine")
    parts = [("eyes", spec.get("eyes_mesh", EYES_MESH), "Eyes"), ("eyebrows", spec.get("eyebrows"), "Eyebrows"),
             ("eyelashes", spec.get("eyelashes"), "Eyelashes"), ("hair", spec.get("hair"), "Hair")]
    if face_dir:
        parts += [("teeth", spec.get("teeth", "teeth_base"), "Teeth"), ("tongue", spec.get("tongue", "tongue01"), "Tongue")]
    parts += [("clothes", c, "Clothes") for c in spec.get("clothes", [])]
    followers = []
    for subdir, name, kind in parts:
        if name:
            ob = HumanService.add_mhclo_asset(find(AssetService, name, subdir, ".mhclo"), body, asset_type=kind,
                                              subdiv_levels=0, material_type="GAMEENGINE")
            if ob is not None and kind == "Eyes" and spec.get("eye_colour"):
                eye_colour(bpy, ob, lambda n: find(AssetService, n, "eyes/materials", ".png"), spec["eye_colour"])
            if ob is not None and kind in ("Eyebrows", "Eyelashes"):
                followers.append((ob, ("eye", "brow", "cheek")))
            elif ob is not None and kind in ("Teeth", "Tongue"):
                followers.append((ob, ("jaw", "mouth", "tongue")))
    face = face_units(bpy, service, body, face_dir, followers) if face_dir else []

    # The export copy: the hidden faces under the clothes and the helper geometry gone, modifiers applied.
    root = ExportService.create_character_copy(body, name_suffix="_export")
    export_body = ObjectService.find_object_of_type_amongst_nearest_relatives(root, "Basemesh")
    ExportService.bake_modifiers_remove_helpers(export_body, bake_masks=True, bake_subdiv=True, remove_helpers=True, also_proxy=True)
    # (the hair's, eyebrows' and eyelashes' alpha cuts the surface; the others are opaque)
    # (milestone 243: and the high-poly eyes' cornea, transparent in their texture)
    cut = [n for n in (spec.get("hair"), spec.get("eyebrows"), spec.get("eyelashes")) if n]
    if spec.get("eyes_mesh", EYES_MESH) == "high-poly":
        cut.append("high-poly")
    game_materials(bpy, root, cut)
    apply_shape_keys(bpy, root, set(face))
    trim_under_hat(bpy, root, spec.get("hair"), spec.get("clothes", []))
    humanoid_bones(root)
    eye_bones(bpy, root, spec.get("eyes_mesh", EYES_MESH))
    os.makedirs(out, exist_ok=True)
    if SKIN_AO:
        cards = [n for n in (spec.get("hair"), spec.get("eyebrows"), spec.get("eyelashes")) if n]
        mean = skin_ao(bpy, root, export_body, os.path.join(out, spec["name"] + "_ao.png"), cards)
        print("make_characters: %s's skin occlusion baked (mean %.3f)" % (spec["name"], mean))
    print("make_characters: %s's detail UVs: %d islands" % (spec["name"], detail_uv(bpy, export_body)))
    if spec.get("hair") and hair_dump != "skip":
        n = hair_strands(bpy, root, export_body, spec["hair"], os.path.join(out, spec["name"] + "_hair.json"), hair_dump)
        print("make_characters: %s's hair: %d strand guides" % (spec["name"], n))
    bpy.ops.object.select_all(action="DESELECT")
    root.select_set(True)
    for child in ObjectService.get_list_of_children(root):
        child.select_set(True)
    bpy.context.view_layer.objects.active = root
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, spec["name"] + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, use_selection=True, export_format="GLB", export_skins=True,
                              export_animations=False, export_morph=bool(face), export_morph_normal=False,
                              export_image_format="JPEG",
                              export_jpeg_quality=88, export_apply=False)
    repack_images(path)
    if save_blend:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, spec["name"] + ".blend"))
    return path


def main():
    args = parse_args()
    with open(args.spec, encoding="utf-8") as f:
        people = json.load(f)["people"]
    only = [n for n in args.only.split(",") if n]
    global SKIN_AO
    SKIN_AO = args.ao != "0"
    bpy, service = setup_mpfb(args)
    face_dir = ""
    if args.faces != "none":
        face_dir = os.path.abspath(args.faces) if args.faces else os.path.join(os.path.abspath(args.assets), "targets", "faceunits")
        if not os.path.isdir(face_dir):
            print("make_characters: no face units at %s (the people without face shapes)" % face_dir)
            face_dir = ""
    for spec in people:
        if only and spec["name"] not in only:
            continue
        path = make(bpy, service, spec, os.path.abspath(args.out), args.blend, face_dir, args.hair_dump)
        print("make_characters: %s (%d KB)" % (path, os.path.getsize(path) // 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
