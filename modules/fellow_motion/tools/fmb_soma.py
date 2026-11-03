#!/usr/bin/env python3
# FellowEngine — the SOMA engine's glue around the model, in NumPy (the reference of FellowMotionSOMA in C++):
# local joint transforms in, the model's inputs; its outputs back to local joint transforms, endpoint-corrected.
# Mirrors MotionBricks' SOMA engine (encode / decode / endpoint correction kernels). Y up, metres, xyzw quaternions.

import numpy as np

from fmb_bvh import MODEL_JOINTS, quat_mul

SAMPLE_RATE = 30.0
BOUNDARY = 3


def qconj(q):
    return q * np.array([-1, -1, -1, 1.0])


def qrot(q, v):
    u = q[..., :3]
    w = q[..., 3:4]
    t = 2 * np.cross(u, v)
    return v + w * t + np.cross(u, t)


def qaxis_y(a):
    return np.stack([np.zeros_like(a), np.sin(a / 2), np.zeros_like(a), np.cos(a / 2)], -1)


def qmat(q):
    x, y, z, w = np.moveaxis(q, -1, 0)
    return np.stack([np.stack([1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], -1),
                     np.stack([2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], -1),
                     np.stack([2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)], -1)], -2)


def mat_quat(m):
    # Shepperd's method, xyzw.
    m = np.asarray(m, np.float64)
    out = np.zeros(m.shape[:-2] + (4,))
    tr = m[..., 0, 0] + m[..., 1, 1] + m[..., 2, 2]
    for idx in np.ndindex(m.shape[:-2]):
        a = m[idx]
        t = tr[idx]
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
        out[idx] = q
    return out / np.linalg.norm(out, axis=-1, keepdims=True)


class Skeleton:
    def __init__(self, names, parents, ref_pos, ref_rot):
        self.names = list(names)
        self.parents = np.asarray(parents)
        self.ref_pos = np.asarray(ref_pos, np.float64)
        self.ref_rot = np.asarray(ref_rot, np.float64)
        idx = {n: i for i, n in enumerate(self.names)}
        self.model_to_pose = np.array([idx[n] for n in MODEL_JOINTS])
        assert self.model_to_pose[0] == 0
        self.pose_to_model = np.full(len(self.names), -1)
        for mj, pj in enumerate(self.model_to_pose):
            p = self.parents[pj]
            if p == -1 or self.pose_to_model[p] >= 0:
                self.pose_to_model[pj] = mj
        self.right_leg = idx["RightLeg"]
        self.left_leg = idx["LeftLeg"]

    def world(self, lp, lq):
        """Local -> world, for poses [..., J, 3/4]."""
        wp = np.zeros_like(lp)
        wq = np.zeros_like(lq)
        for j, p in enumerate(self.parents):
            if p < 0:
                wp[..., j, :] = lp[..., j, :]
                wq[..., j, :] = lq[..., j, :]
            else:
                wq[..., j, :] = quat_mul(wq[..., p, :], lq[..., j, :])
                wp[..., j, :] = wp[..., p, :] + qrot(wq[..., p, :], lp[..., j, :])
        return wp, wq


def heading_from_legs(right, left):
    ax = right[..., 0] - left[..., 0]
    az = right[..., 2] - left[..., 2]
    ln = np.sqrt(ax * ax + az * az)
    # forward = up x across = (az, 0, -ax); heading = atan2(forward.x, forward.z)
    h = np.arctan2(az / np.maximum(ln, 1e-12), -ax / np.maximum(ln, 1e-12))
    return np.where(ln <= 1e-6, 0.0, h)


def encode(skel, ctx, tgt, num_target_poses, allowed_counts=range(8, 15), heading_alignment=(1.0, 0.0)):
    """ctx, tgt: (local positions [4, J, 3], local rotations [4, J, 4]) in world space -> ONNX inputs (batch 1) and
    the alignment transform (p, q)."""
    lp = np.concatenate([ctx[0], tgt[0]], 0)
    lq = np.concatenate([ctx[1], tgt[1]], 0)
    wp, wq = skel.world(lp, lq)
    hips = wp[3, 0]
    cur = heading_from_legs(wp[3, skel.right_leg], wp[3, skel.left_leg])
    desired = np.arctan2(heading_alignment[0], heading_alignment[1])
    al_p = np.array([hips[0], 0.0, hips[2]])
    al_q = qaxis_y(np.array(cur - desired))
    inv = qconj(al_q)
    ap = qrot(inv, wp - al_p)  # aligned positions [8, J, 3]
    aq = quat_mul(np.broadcast_to(inv, wq.shape), wq)
    grv = np.zeros((1, 8, 5), np.float32)
    lrv = np.zeros((1, 8, 4), np.float32)
    lpose = np.zeros((1, 8, 267), np.float32)
    heads = heading_from_legs(ap[:, skel.right_leg], ap[:, skel.left_leg])
    for f in range(8):
        nf = f if f == 3 else min(f + 1, 7)
        h = ap[f, 0]
        hn = ap[nf, 0]
        dh = np.arctan2(np.sin(heads[nf] - heads[f]), np.cos(heads[nf] - heads[f]))
        grv[0, f] = [h[0], h[1], h[2], np.cos(heads[f]), np.sin(heads[f])]
        lrv[0, f] = [dh * SAMPLE_RATE, (hn[0] - h[0]) * SAMPLE_RATE, (hn[2] - h[2]) * SAMPLE_RATE, h[1]]
        root = np.array([h[0], 0.0, h[2]])
        for mj, pj in enumerate(skel.model_to_pose):
            if mj > 0:
                lpose[0, f, (mj - 1) * 3:(mj - 1) * 3 + 3] = ap[f, pj] - root
            m = qmat(aq[f, pj])
            lpose[0, f, 87 + mj * 6:87 + mj * 6 + 3] = m[:, 0]
            lpose[0, f, 87 + mj * 6 + 3:87 + mj * 6 + 6] = m[:, 1]
    ntp = num_target_poses
    hg = np.array([[1 if i < 4 + ntp else 0 for i in range(8)]], np.int32)
    hl = hg.copy()
    hl[0, 3] = 0
    hl[0, 4 + ntp - 1] = 0
    allowed = np.zeros((1, 11), np.int32)
    for c in allowed_counts:
        allowed[0, c - 6] = 1
    inputs = dict(global_root_values=grv, has_global_root_values=hg, local_root_values=lrv, has_local_root_values=hl,
                  local_poses=lpose, has_local_poses=hg.copy(), allowed_pred_num_tokens=allowed)
    return inputs, (al_p, al_q)


def rot6d(c):
    x = c[..., 0:3] / np.linalg.norm(c[..., 0:3], axis=-1, keepdims=True)
    z = np.cross(x, c[..., 3:6])
    z = z / np.linalg.norm(z, axis=-1, keepdims=True)
    y = np.cross(z, x)
    return mat_quat(np.stack([x, y, z], -1))


def decode(skel, motion, num_tokens, alignment):
    """pred_global_motions [64, 366] of one request -> local transforms [F, J, 3/4] and foot contacts [F, 4]."""
    al_p, al_q = alignment
    raw = int(num_tokens) * 4
    F = max(0, raw - 2 * BOUNDARY)
    J = len(skel.names)
    op = np.repeat(skel.ref_pos[None], F, 0).copy()
    oq = np.repeat(skel.ref_rot[None], F, 0).copy()
    fc = np.zeros((F, 4))
    for i in range(F):
        r = BOUNDARY + i
        fc[i] = np.clip(motion[r, 362:366], 0, 1)
        rots = rot6d(motion[r, 92:272].reshape(30, 6))  # model-space global rotations
        hp = motion[r, 0:3]
        op[i, 0] = al_p + qrot(al_q, hp)
        oq[i, 0] = quat_mul(al_q, rots[0])
        for j in range(1, J):
            mj = skel.pose_to_model[j]
            if mj < 0:
                continue
            pm = skel.pose_to_model[skel.parents[j]]
            lq = quat_mul(qconj(rots[pm]), rots[mj])
            oq[i, j] = lq / np.linalg.norm(lq)
            op[i, j] = skel.ref_pos[j]
    return op, oq, fc


def slerp(a, b, w):
    d = (a * b).sum(-1, keepdims=True)
    b = np.where(d < 0, -b, b)
    d = np.abs(d)
    th = np.arccos(np.clip(d, -1, 1))
    s = np.sin(th)
    small = s < 1e-6
    wa = np.where(small, 1 - w, np.sin((1 - w) * th) / np.where(small, 1, s))
    wb = np.where(small, w, np.sin(w * th) / np.where(small, 1, s))
    q = a * wa + b * wb
    return q / np.linalg.norm(q, axis=-1, keepdims=True)


def correct_endpoints(op, oq, ctx_last, tgt_first):
    """Spread the deltas that move the first frame onto the last context pose and the last frame onto the first
    target pose over the motion (MotionBricks' endpoint correction)."""
    F = op.shape[0]
    if F < 2:
        return op, oq
    J = op.shape[1]
    cp, cq = ctx_last
    tp, tq = tgt_first
    I = np.array([0, 0, 0, 1.0])
    # Root: world-space position offsets, rotation applied after.
    s_dp = cp[0] - op[0, 0]
    s_dq = quat_mul(qconj(oq[0, 0]), cq[0])
    e_dp = tp[0] - op[-1, 0]
    e_dq = quat_mul(qconj(oq[-1, 0]), tq[0])
    # Joints: delta = inv(src) * dst.
    def delta(sp, sq, dp, dq):
        iq = qconj(sq)
        return qrot(iq, dp - sp), quat_mul(iq, dq)
    sd = [delta(op[0, j], oq[0, j], cp[j], cq[j]) for j in range(J)]
    ed = [delta(op[-1, j], oq[-1, j], tp[j], tq[j]) for j in range(J)]
    np_, nq = op.copy(), oq.copy()
    for f in range(F):
        we = f / (F - 1)
        ws = 1 - we
        q = quat_mul(quat_mul(oq[f, 0], slerp(I, s_dq, ws)), slerp(I, e_dq, we))
        nq[f, 0] = q / np.linalg.norm(q)
        np_[f, 0] = op[f, 0] + s_dp * ws + e_dp * we
        for j in range(1, J):
            p, q = op[f, j], oq[f, j]
            for (dp, dq), w in ((sd[j], ws), (ed[j], we)):
                bp = dp * w
                bq = slerp(I, dq, w)
                p = p + qrot(q, bp)
                q = quat_mul(q, bq)
            np_[f, j] = p
            nq[f, j] = q / np.linalg.norm(q)
    return np_, nq
