#!/usr/bin/env python3
# FellowEngine — the SOMA skeleton and body mesh from MotionBricks' USD asset (motionbricks/assets/human/
# soma_base_skel_minimal_no_root.usd, CC-BY-4.0 by NVIDIA; not part of this repository): the skeleton the engine
# drives (into the .fmb) and a skinned mannequin as glTF binary (.glb) for Godot to load at run time.
#
# Needs usd-core (pip install usd-core) and numpy.

import json
import struct

import numpy as np

ATTRIBUTION = ("SOMA body mesh and skeleton: NVIDIA MotionBricks (soma_base_skel_minimal_no_root.usd), CC-BY-4.0, "
               "converted by FellowEngine's fmb_usd.py")


def _rows_to_trs(m):
    """USD row-vector matrix -> (translation, xyzw quaternion), metres."""
    m = np.array(m, np.float64)
    R = m[:3, :3].T  # column-vector rotation
    U, _, Vt = np.linalg.svd(R)
    R = U @ Vt
    return m[3, :3] * 0.01, _mat_quat(R)


def _mat_quat(a):
    t = a[0, 0] + a[1, 1] + a[2, 2]
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        q = [(a[2, 1] - a[1, 2]) / s, (a[0, 2] - a[2, 0]) / s, (a[1, 0] - a[0, 1]) / s, 0.25 * s]
    elif a[0, 0] > a[1, 1] and a[0, 0] > a[2, 2]:
        s = np.sqrt(1.0 + a[0, 0] - a[1, 1] - a[2, 2]) * 2
        q = [0.25 * s, (a[0, 1] + a[1, 0]) / s, (a[0, 2] + a[2, 0]) / s, (a[2, 1] - a[1, 2]) / s]
    elif a[1, 1] > a[2, 2]:
        s = np.sqrt(1.0 + a[1, 1] - a[0, 0] - a[2, 2]) * 2
        q = [(a[0, 1] + a[1, 0]) / s, 0.25 * s, (a[1, 2] + a[2, 1]) / s, (a[0, 2] - a[2, 0]) / s]
    else:
        s = np.sqrt(1.0 + a[2, 2] - a[0, 0] - a[1, 1]) * 2
        q = [(a[0, 2] + a[2, 0]) / s, (a[1, 2] + a[2, 1]) / s, 0.25 * s, (a[1, 0] - a[0, 1]) / s]
    q = np.array(q)
    return q / np.linalg.norm(q)


def load(path):
    from pxr import Usd, UsdGeom, UsdSkel

    st = Usd.Stage.Open(path)
    skel = None
    mesh = None
    anim = None
    for p in st.Traverse():
        if p.IsA(UsdSkel.Skeleton) and skel is None:
            skel = UsdSkel.Skeleton(p)
        elif p.IsA(UsdGeom.Mesh) and mesh is None:
            mesh = UsdGeom.Mesh(p)
        elif p.IsA(UsdSkel.Animation) and anim is None:
            anim = UsdSkel.Animation(p)
    paths = [str(j) for j in skel.GetJointsAttr().Get()]
    names = [p.split("/")[-1] for p in paths]
    parents = [paths.index(p.rsplit("/", 1)[0]) if "/" in p else -1 for p in paths]
    rest = [_rows_to_trs(m) for m in skel.GetRestTransformsAttr().Get()]
    bind = [np.array(m, np.float64) for m in skel.GetBindTransformsAttr().Get()]
    relaxed = None
    if anim is not None:
        aj = [str(j).split("/")[-1] for j in anim.GetJointsAttr().Get()]
        rots = anim.GetRotationsAttr().Get(Usd.TimeCode.EarliestTime()) or anim.GetRotationsAttr().Get()
        if rots is not None:
            relaxed = {}
            for n, q in zip(aj, rots):
                im = q.GetImaginary()
                relaxed[n] = [float(im[0]), float(im[1]), float(im[2]), float(q.GetReal())]
    skeleton = dict(
        names=names, parents=parents,
        rest_pos=[r[0].tolist() for r in rest], rest_rot=[r[1].tolist() for r in rest],
        relaxed_rot=[relaxed.get(n, rest[i][1].tolist()) if relaxed else rest[i][1].tolist()
                     for i, n in enumerate(names)],
        attribution=ATTRIBUTION,
    )
    pts = np.array(mesh.GetPointsAttr().Get(), np.float64) * 0.01
    counts = np.array(mesh.GetFaceVertexCountsAttr().Get())
    idx = np.array(mesh.GetFaceVertexIndicesAttr().Get())
    tris = []
    o = 0
    for c in counts:
        f = idx[o:o + c]
        for k in range(1, c - 1):
            tris.append((f[0], f[k], f[k + 1]))
        o += c
    tris = np.array(tris, np.uint32)
    if mesh.GetOrientationAttr().Get() == "leftHanded":
        tris = tris[:, ::-1]
    b = UsdSkel.BindingAPI(mesh.GetPrim())
    es = b.GetJointIndicesPrimvar().GetElementSize()
    ji = np.array(b.GetJointIndicesPrimvar().Get()).reshape(-1, es)
    jw = np.array(b.GetJointWeightsPrimvar().Get(), np.float64).reshape(-1, es)
    mj = b.GetJointsAttr().Get() if b.GetJointsAttr() else None
    if mj:
        remap = np.array([paths.index(str(p)) for p in mj])
        ji = remap[ji]
    gb = b.GetGeomBindTransformAttr().Get()
    if gb is not None:
        g = np.array(gb, np.float64)
        pts = (np.concatenate([pts / 0.01, np.ones((len(pts), 1))], 1) @ g)[:, :3] * 0.01
    return skeleton, dict(points=pts, tris=tris, joints=ji, weights=jw, bind=bind)


def _normals(p, t):
    n = np.zeros_like(p)
    fn = np.cross(p[t[:, 1]] - p[t[:, 0]], p[t[:, 2]] - p[t[:, 0]])
    for k in range(3):
        np.add.at(n, t[:, k], fn)
    ln = np.linalg.norm(n, axis=1, keepdims=True)
    return n / np.maximum(ln, 1e-12)


def write_glb(path, skeleton, mesh, color=(0.62, 0.66, 0.72, 1.0)):
    names, parents = skeleton["names"], skeleton["parents"]
    J = len(names)
    p = mesh["points"].astype(np.float32)
    t = mesh["tris"].astype(np.uint32)
    nrm = _normals(mesh["points"], mesh["tris"]).astype(np.float32)
    # 8 strongest influences, renormalized.
    order = np.argsort(-mesh["weights"], axis=1)[:, :8]
    w = np.take_along_axis(mesh["weights"], order, 1)
    j = np.take_along_axis(mesh["joints"], order, 1)
    w = w / np.maximum(w.sum(1, keepdims=True), 1e-12)
    w = w.astype(np.float32)
    j = j.astype(np.uint16)
    # Inverse bind matrices (column-major mat4): bind world in metres.
    ibm = []
    for m in mesh["bind"]:
        M = np.array(m, np.float64).T.copy()  # column-vector matrix
        M[:3, 3] *= 0.01
        ibm.append(np.linalg.inv(M).T.reshape(-1))  # glTF stores column-major
    ibm = np.array(ibm, np.float32)

    blobs = []
    views = []
    accessors = []

    def add(arr, target=None, comp=5126, typ="VEC3", minmax=False):
        data = arr.tobytes()
        off = sum(len(b) for b in blobs)
        pad = (-off) % 4
        if pad:
            blobs.append(b"\0" * pad)
            off += pad
        blobs.append(data)
        v = dict(buffer=0, byteOffset=off, byteLength=len(data))
        if target:
            v["target"] = target
        views.append(v)
        a = dict(bufferView=len(views) - 1, componentType=comp, count=int(arr.shape[0]), type=typ)
        if minmax:
            a["min"] = arr.min(0).tolist()
            a["max"] = arr.max(0).tolist()
        accessors.append(a)
        return len(accessors) - 1

    a_pos = add(p, 34962, minmax=True)
    a_nrm = add(nrm, 34962)
    a_j0 = add(np.ascontiguousarray(j[:, :4]), 34962, 5123, "VEC4")
    a_w0 = add(np.ascontiguousarray(w[:, :4]), 34962, 5126, "VEC4")
    a_j1 = add(np.ascontiguousarray(j[:, 4:]), 34962, 5123, "VEC4")
    a_w1 = add(np.ascontiguousarray(w[:, 4:]), 34962, 5126, "VEC4")
    a_idx = add(t.reshape(-1), 34963, 5125, "SCALAR")
    a_ibm = add(ibm, None, 5126, "MAT4")
    nodes = []
    for i in range(J):
        rp, rq = skeleton["rest_pos"][i], skeleton["rest_rot"][i]
        nodes.append(dict(name=names[i], translation=list(map(float, rp)), rotation=list(map(float, rq)),
                          children=[c for c in range(J) if parents[c] == i]))
    mesh_node = len(nodes)
    nodes.append(dict(name="SomaBody", mesh=0, skin=0))
    roots = [i for i in range(J) if parents[i] < 0]
    gl = dict(
        asset=dict(version="2.0", generator="FellowEngine fmb_usd.py", copyright=ATTRIBUTION),
        scene=0, scenes=[dict(nodes=roots + [mesh_node])], nodes=nodes,
        meshes=[dict(name="SomaBody", primitives=[dict(attributes=dict(POSITION=a_pos, NORMAL=a_nrm, JOINTS_0=a_j0,
                                                                       WEIGHTS_0=a_w0, JOINTS_1=a_j1,
                                                                       WEIGHTS_1=a_w1), indices=a_idx,
                                                       material=0)])],
        materials=[dict(name="Mannequin", pbrMetallicRoughness=dict(baseColorFactor=list(color), metallicFactor=0.0,
                                                                     roughnessFactor=0.55))],
        skins=[dict(name="SomaSkin", joints=list(range(J)), inverseBindMatrices=a_ibm, skeleton=roots[0])],
        accessors=accessors, bufferViews=views,
        buffers=[dict(byteLength=sum(len(b) for b in blobs))],
    )
    js = json.dumps(gl, separators=(",", ":")).encode()
    js += b" " * ((-len(js)) % 4)
    bin_ = b"".join(blobs)
    bin_ += b"\0" * ((-len(bin_)) % 4)
    total = 12 + 8 + len(js) + 8 + len(bin_)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A))
        f.write(js)
        f.write(struct.pack("<II", len(bin_), 0x004E4942))
        f.write(bin_)
