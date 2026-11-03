#!/usr/bin/env python3
# FellowEngine — a BVH reader for the SOMA clips (Y up; centimetres -> metres; degrees, channels composed in the
# listed order, q = q(ch0) q(ch1) q(ch2)), and the skeleton description the .fmb carries.
#
# MIT licensed like the engine. The SOMA clips themselves (NVIDIA Sample Data Evaluation License) are not part of
# this repository.

import re

import numpy as np

# The model's 30 joints, in its order (MotionBricks soma_n1.json).
MODEL_JOINTS = ["Hips", "Spine1", "Spine2", "Chest", "Neck1", "Neck2", "Head", "Jaw", "LeftEye", "RightEye",
                "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand", "LeftHandThumbEnd", "LeftHandMiddleEnd",
                "RightShoulder", "RightArm", "RightForeArm", "RightHand", "RightHandThumbEnd", "RightHandMiddleEnd",
                "LeftLeg", "LeftShin", "LeftFoot", "LeftToeBase", "RightLeg", "RightShin", "RightFoot", "RightToeBase"]


def quat_mul(a, b):
    # xyzw
    ax, ay, az, aw = np.moveaxis(a, -1, 0)
    bx, by, bz, bw = np.moveaxis(b, -1, 0)
    return np.stack([aw * bx + ax * bw + ay * bz - az * by,
                     aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw,
                     aw * bw - ax * bx - ay * by - az * bz], -1)


def quat_axis(axis, ang):
    h = ang * 0.5
    s = np.sin(h)
    q = np.zeros(ang.shape + (4,), np.float64)
    q[..., "XYZ".index(axis)] = s
    q[..., 3] = np.cos(h)
    return q


class BVH:
    def __init__(self, path):
        text = open(path).read()
        head, motion = text.split("MOTION")
        self.names, self.parents, self.offsets, self.channels = [], [], [], []
        stack = []
        tok = re.findall(r"\S+", head)
        i = 0
        last = -1
        while i < len(tok):
            t = tok[i]
            if t in ("ROOT", "JOINT"):
                name = tok[i + 1].split(":")[-1]
                self.names.append(name)
                self.parents.append(stack[-1] if stack else -1)
                self.offsets.append([0, 0, 0])
                self.channels.append([])
                last = len(self.names) - 1
                i += 2
            elif t == "End":
                # End Site { OFFSET x y z }
                depth = 0
                while True:
                    i += 1
                    if tok[i] == "{":
                        depth += 1
                    elif tok[i] == "}":
                        depth -= 1
                        if depth == 0:
                            break
                i += 1
                last = -2
            elif t == "{":
                if last >= 0:
                    stack.append(last)
                i += 1
            elif t == "}":
                stack.pop()
                i += 1
            elif t == "OFFSET":
                self.offsets[last] = [float(tok[i + 1]), float(tok[i + 2]), float(tok[i + 3])]
                i += 4
            elif t == "CHANNELS":
                n = int(tok[i + 1])
                self.channels[last] = tok[i + 2:i + 2 + n]
                i += 2 + n
            else:
                i += 1
        lines = motion.strip().split("\n")
        self.num_frames = int(lines[0].split(":")[1])
        self.frame_time = float(lines[1].split(":")[1])
        self.rate = round(1.0 / self.frame_time, 2)
        data = np.array([[float(x) for x in l.split()] for l in lines[2:2 + self.num_frames]], np.float64)
        self.offsets = np.array(self.offsets, np.float64) * 0.01
        J = len(self.names)
        self.pos = np.repeat(self.offsets[None], self.num_frames, 0).copy()
        self.rot = np.zeros((self.num_frames, J, 4))
        self.rot[..., 3] = 1
        c = 0
        for j, ch in enumerate(self.channels):
            q = np.zeros((self.num_frames, 4))
            q[:, 3] = 1
            for name in ch:
                v = data[:, c]
                c += 1
                if name.endswith("position"):
                    self.pos[:, j, "XYZ".index(name[0])] = v * 0.01
                else:
                    q = quat_mul(q, quat_axis(name[0], np.radians(v)))
            self.rot[:, j] = q
        self.index = {n: i for i, n in enumerate(self.names)}

    def sample(self, t):
        """Local transforms at time t (s): positions lerped, rotations slerped (shortest), clamped."""
        f = np.clip(t * self.rate, 0, self.num_frames - 1)
        a = int(np.floor(f))
        b = min(a + 1, self.num_frames - 1)
        w = f - a
        p = self.pos[a] * (1 - w) + self.pos[b] * w
        qa, qb = self.rot[a], self.rot[b]
        d = (qa * qb).sum(-1, keepdims=True)
        qb = np.where(d < 0, -qb, qb)
        d = np.abs(d)
        th = np.arccos(np.clip(d, -1, 1))
        s = np.sin(th)
        small = s < 1e-6
        wa = np.where(small, 1 - w, np.sin((1 - w) * th) / np.where(small, 1, s))
        wb = np.where(small, w, np.sin(w * th) / np.where(small, 1, s))
        q = qa * wa + qb * wb
        q /= np.linalg.norm(q, axis=-1, keepdims=True)
        return p, q


def skeleton_json(path):
    """The skeleton the engine builds (rooted at Hips: the clips' "Root" above it is the identity), rest = frame 0's
    joint offsets (the positions of joints without position channels are their offsets)."""
    b = BVH(path)
    keep = [i for i, n in enumerate(b.names) if n != "Root"]
    remap = {old: new for new, old in enumerate(keep)}
    joints = []
    for old in keep:
        p = b.parents[old]
        joints.append(dict(name=b.names[old], parent=remap.get(p, -1), offset=b.offsets[old].tolist(),
                           rest_rotation=[0, 0, 0, 1]))
    return dict(joints=joints, model_joints=MODEL_JOINTS)
