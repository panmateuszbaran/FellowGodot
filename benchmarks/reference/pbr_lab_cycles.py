"""Build the PBR Validation Lab in Blender from the Godot export and render Cycles reference frames.

Usage (background Blender):
  blender --background --factory-startup --python-exit-code 1 --python benchmarks/reference/pbr_lab_cycles.py -- \
      --scene <dir>/pbr_lab_scene.json --out <dir> [--samples 2048] [--res 1920x1080] [--cameras a,b] [--device OPTIX]

Input is produced by the benchmark project itself (identical geometry, UVs, material parameters, textures,
lights and cameras):
  godot --path benchmarks/project -- --scene=pbr --mode=list --lighting=ref --export-ref=<dir>

Equivalences (documented in benchmarks/reference/README.md):
  * Godot is Y-up / -Z forward, Blender is Z-up: p_blender = (x, -z, y).
  * Godot directional energy E (non-physical units) -> irradiance PI*E -> Blender sun strength PI*E [W/m2].
  * Godot emission radiance = colour * energy -> Principled emission colour/strength identical.
  * Sky: uniform radiance L (linear) -> World background colour L, strength 1.
  * Materials: Principled BSDF (base colour linear, metallic, roughness, IOR 1.5 = Godot specular 0.5 / F0 0.04).
    Godot lab materials use Lambert diffuse to match Principled v2.
  * Alpha blend -> Principled alpha; alpha scissor -> step(alpha, threshold).
Known, EXPECTED differences (not rendering bugs): Cycles computes full multi-bounce GI (Godot has no GI enabled in
this preset, only sky ambient + SSAO), emissive surfaces illuminate neighbours in Cycles only, and transparent
objects refract/absorb nothing in either renderer (Principled alpha is used, not glass).

Outputs: <out>/cycles_<camera>.exr (linear scene radiance, 32-bit float) + cycles_meta.json.
"""

import json
import math
import os
import sys
import time

import bpy
import mathutils

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def arg(name, default=None):
    key = "--" + name
    if key in argv:
        return argv[argv.index(key) + 1]
    return default


# Absolute paths: Blender does not resolve relative image paths against the process working directory.
SCENE_JSON = os.path.abspath(arg("scene")) if arg("scene") else None
OUT = os.path.abspath(arg("out", os.path.dirname(SCENE_JSON) if SCENE_JSON else "."))
SAMPLES = int(arg("samples", "2048"))
RES = arg("res", "1920x1080")
CAMS = arg("cameras", "")
DEVICE = arg("device", "OPTIX")

C = mathutils.Matrix(((1, 0, 0), (0, 0, -1), (0, 1, 0)))  # Godot -> Blender axis change


def to_b_vec(v):
    return C @ mathutils.Vector(v)


def to_b_matrix(w):
    # w: [bxx, byx, bzx, ox, bxy, byy, bzy, oy, bxz, byz, bzz, oz] (Godot row-major 3x4: basis columns x,y,z + origin)
    m3 = mathutils.Matrix(((w[0], w[1], w[2]), (w[4], w[5], w[6]), (w[8], w[9], w[10])))
    o = mathutils.Vector((w[3], w[7], w[11]))
    mb = C @ m3 @ C.inverted()
    ob = C @ o
    m4 = mb.to_4x4()
    m4.translation = ob
    return m4


def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def build_mesh(key, md):
    mesh = bpy.data.meshes.new("m_" + key[:8])
    pos = md["positions"]
    verts = [to_b_vec((pos[i], pos[i + 1], pos[i + 2])) for i in range(0, len(pos), 3)]
    idx = md["indices"] or list(range(len(verts)))
    # Godot uses clockwise front faces; Blender counter-clockwise -> reverse each triangle.
    faces = [(idx[i], idx[i + 2], idx[i + 1]) for i in range(0, len(idx), 3)]
    mesh.from_pydata([tuple(v) for v in verts], [], faces)
    mesh.update()
    uvs = md["uvs"]
    if uvs:
        uvl = mesh.uv_layers.new(name="UVMap")
        for poly in mesh.polygons:
            for li in poly.loop_indices:
                vi = mesh.loops[li].vertex_index
                # Godot UV origin is top-left, Blender bottom-left.
                uvl.data[li].uv = (uvs[vi * 2], 1.0 - uvs[vi * 2 + 1])
    nrm = md["normals"]
    if nrm:
        normals = []
        for poly in mesh.polygons:
            for li in poly.loop_indices:
                vi = mesh.loops[li].vertex_index
                normals.append(tuple(to_b_vec((nrm[vi * 3], nrm[vi * 3 + 1], nrm[vi * 3 + 2])).normalized()))
        mesh.normals_split_custom_set(normals)
    for p in mesh.polygons:
        p.use_smooth = True
    return mesh


_img_cache = {}


def load_image(path, non_color):
    key = (path, non_color)
    if key not in _img_cache:
        img = bpy.data.images.load(path, check_existing=False)
        img.colorspace_settings.name = "Non-Color" if non_color else "sRGB"
        _img_cache[key] = img
    return _img_cache[key]


def build_material(name, m, tex_dir, textures):
    mat = bpy.data.materials.new("mat_" + name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    base = m.get("albedo", [0.5, 0.5, 0.5])
    bsdf.inputs["Base Color"].default_value = (base[0], base[1], base[2], 1.0)
    bsdf.inputs["Metallic"].default_value = m.get("metallic", 0.0)
    bsdf.inputs["Roughness"].default_value = m.get("roughness", 0.5)
    bsdf.inputs["IOR"].default_value = 1.5
    uv = None
    if "uv_scale" in m or "albedo_tex" in m or "normal_tex" in m:
        tc = nt.nodes.new("ShaderNodeTexCoord")
        mp = nt.nodes.new("ShaderNodeMapping")
        s = m.get("uv_scale", [1.0, 1.0])
        mp.inputs["Scale"].default_value = (s[0], s[1], 1.0)
        # Godot scales around the top-left UV origin; Blender (flipped V) around bottom-left -> compensate offset.
        mp.inputs["Location"].default_value = (0.0, 1.0 - s[1], 0.0)
        nt.links.new(tc.outputs["UV"], mp.inputs["Vector"])
        uv = mp.outputs["Vector"]
    alpha_socket = None
    if "albedo_tex" in m:
        tx = nt.nodes.new("ShaderNodeTexImage")
        tx.image = load_image(os.path.join(tex_dir, textures[m["albedo_tex"]]), False)
        tx.interpolation = "Linear"
        nt.links.new(uv, tx.inputs["Vector"])
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        mix.blend_type = "MULTIPLY"
        mix.inputs["Factor"].default_value = 1.0
        nt.links.new(tx.outputs["Color"], mix.inputs["A"])
        mix.inputs["B"].default_value = (base[0], base[1], base[2], 1.0)
        nt.links.new(mix.outputs["Result"], bsdf.inputs["Base Color"])
        alpha_socket = tx.outputs["Alpha"]
    if "normal_tex" in m:
        tn = nt.nodes.new("ShaderNodeTexImage")
        tn.image = load_image(os.path.join(tex_dir, textures[m["normal_tex"]]), True)
        nt.links.new(uv, tn.inputs["Vector"])
        nm = nt.nodes.new("ShaderNodeNormalMap")
        nm.inputs["Strength"].default_value = m.get("normal_scale", 1.0)
        nt.links.new(tn.outputs["Color"], nm.inputs["Color"])
        nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    if "emission" in m:
        e = m["emission"]
        bsdf.inputs["Emission Color"].default_value = (e[0], e[1], e[2], 1.0)
        bsdf.inputs["Emission Strength"].default_value = m.get("emission_energy", 1.0)
    mode = m.get("alpha", "opaque")
    if mode == "blend":
        bsdf.inputs["Alpha"].default_value = m.get("alpha_value", 1.0)
    elif mode in ("scissor", "hash") and alpha_socket is not None:
        gt = nt.nodes.new("ShaderNodeMath")
        gt.operation = "GREATER_THAN"
        gt.inputs[1].default_value = m.get("alpha_scissor", 0.5)
        nt.links.new(alpha_socket, gt.inputs[0])
        nt.links.new(gt.outputs["Value"], bsdf.inputs["Alpha"])
    return mat


def main():
    t0 = time.time()
    with open(SCENE_JSON, encoding="utf-8") as f:
        sc = json.load(f)
    tex_dir = os.path.dirname(SCENE_JSON)
    with open(os.path.join(tex_dir, sc["meshes_file"]), encoding="utf-8") as f:
        meshes = json.load(f)
    clear_scene()
    scene = bpy.context.scene
    mesh_cache = {}
    placeholder = bpy.data.materials.new("placeholder")

    def make_object(name, key, mat, matrix):
        if key not in mesh_cache:
            me = build_mesh(key, meshes[key])
            me.materials.append(placeholder)
            mesh_cache[key] = me
        ob = bpy.data.objects.new(name, mesh_cache[key])
        scene.collection.objects.link(ob)
        ob.material_slots[0].link = "OBJECT"
        ob.material_slots[0].material = mat
        ob.matrix_world = matrix
        return ob

    for o in sc["objects"]:
        mat = build_material(o["name"], o["material"], tex_dir, sc["textures"])
        if o["mesh"]["type"] == "grass_cards":
            parent = to_b_matrix(o["world"])
            for i, w in enumerate(o["mesh"]["instances"]):
                make_object(f"grass_{i:03d}", o["mesh_key"], mat, parent @ to_b_matrix(w))
        else:
            make_object(o["name"], o["mesh_key"], mat, to_b_matrix(o["world"]))

    for li in sc["lights"]:
        if li["type"] != "sun":
            continue
        ld = bpy.data.lights.new("sun", "SUN")
        ld.energy = li["irradiance"]
        ld.color = li["color"]
        ld.angle = math.radians(li.get("angular_distance_deg", 0.5))
        ob = bpy.data.objects.new("sun", ld)
        d = to_b_vec(li["direction"]).normalized()
        ob.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        scene.collection.objects.link(ob)

    world = bpy.data.worlds.new("uniform_sky")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    sky = sc["sky_radiance"]
    bg.inputs["Color"].default_value = (sky[0], sky[1], sky[2], 1.0)
    bg.inputs["Strength"].default_value = 1.0
    scene.world = world

    w, h = (int(x) for x in RES.split("x"))
    scene.render.engine = "CYCLES"
    scene.render.resolution_x = w
    scene.render.resolution_y = h
    scene.render.resolution_percentage = 100
    scene.cycles.samples = SAMPLES
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.use_denoising = False
    scene.cycles.filter_width = 1.5
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    scene.view_settings.gamma = 1.0
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = DEVICE
        prefs.get_devices()
        for dev in prefs.devices:
            dev.use = dev.type != "CPU"
        scene.cycles.device = "GPU"
    except Exception as e:  # noqa: BLE001
        print("GPU setup failed, using CPU:", e)

    cams = sc["cameras"]
    wanted = [c for c in cams if not CAMS or c in CAMS.split(",")]
    os.makedirs(OUT, exist_ok=True)
    for name in wanted:
        c = cams[name]
        cd = bpy.data.cameras.new(name)
        cd.sensor_fit = "VERTICAL"
        cd.angle_y = math.radians(c["fov_vertical_deg"])
        cd.clip_start = 0.05
        cd.clip_end = 4000.0
        cob = bpy.data.objects.new(name, cd)
        scene.collection.objects.link(cob)
        pos = to_b_vec(c["pos"])
        tgt = to_b_vec(c["target"])
        cob.location = pos
        cob.rotation_euler = (tgt - pos).to_track_quat("-Z", "Z").to_euler()
        scene.camera = cob
        t1 = time.time()
        scene.render.image_settings.file_format = "OPEN_EXR"
        scene.render.image_settings.color_depth = "32"
        scene.render.image_settings.exr_codec = "ZIP"
        scene.render.filepath = os.path.join(OUT, f"cycles_{name}.exr")
        bpy.ops.render.render(write_still=True)
        print(f"[cycles] {name}: {time.time() - t1:.1f} s ({SAMPLES} spp)", flush=True)
    meta = {"samples": SAMPLES, "resolution": RES, "device": DEVICE, "blender": bpy.app.version_string, "cameras": wanted,
            "total_s": time.time() - t0, "filter_width": 1.5, "max_bounces": scene.cycles.max_bounces,
            "caustics_reflective": scene.cycles.caustics_reflective, "caustics_refractive": scene.cycles.caustics_refractive}
    with open(os.path.join(OUT, "cycles_meta.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)


main()
