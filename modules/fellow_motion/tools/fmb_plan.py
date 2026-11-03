#!/usr/bin/env python3
# FellowEngine — MotionBricks SOMA for the GPU: the op plan, its .fmb file, and a NumPy executor.
#
# The released MotionBricks SOMA checkpoints (NVIDIA Open Model Agreement; not part of this repository) are three
# networks: a root transformer (6 + 2 layers, d 512) with a convolutional decoder, a pose transformer (16 layers,
# d 1024) and a VQ-VAE convolutional decoder. This file turns them into a flat list of GPU ops (GEMMs with gathers
# and fused epilogues, attention, layer norms and a few glue kernels) over one float activation buffer, and runs the
# same plan in NumPy, op for op, as the reference the GPU executor (modules/fellow_motion) must match.
#
# Layouts: activations are channels-last, [request][row][column]; a GEMM weight is [out][K padded to 32], fp16 or
# fp32; a conv1d weight is [out][tap][in]. Biases, norms, embeddings and tables are fp32 "params".
#
# MIT licensed like the engine. It reads the weights; it holds none.

import json
import struct

import numpy as np

FPS = 30.0
EPS_STATS = 1e-05
KPAD = 32
MAGIC = b"FMB1"
VERSION = 1
# The GEMMs' A operand rounded to fp16 (as the cooperative-matrix products on the GPU take it): the reference of that path.
HALF_A = False

# Feature layout of the dual representation's statistics (370): global root pos 3, heading 2 | local root rot vel 1,
# vel 2, y 1 | body: joint positions 87, 6D rotations 180, velocities 90, foot contacts 4.
DUAL_GLOBAL_REP = np.concatenate([np.arange(0, 5), np.arange(9, 370)])
DUAL_LOCAL_REP = np.concatenate([np.arange(5, 9), np.arange(9, 370)])
# The local representation's "joint_positions_and_rotations_and_hip_height": global_root_y, ric, rotations.
LOCAL_POSE_FEAT = np.concatenate([[3], np.arange(4, 91), np.arange(91, 271)])


def _roundup(x, m):
    return (x + m - 1) // m * m


class Builder:
    def __init__(self, weight_type):
        self.weight_type = weight_type
        self.mats = {}  # name -> float32 [out, Kpad]
        self.params = {}  # name -> float32 1-D
        self.acts = {}  # name -> dict(rows, cols, view_of)
        self.ops = []

    # ---- storage ----
    def mat(self, name, w):
        w = np.asarray(w, np.float32)
        assert w.ndim == 2
        k = w.shape[1]
        kp = _roundup(k, KPAD)
        if kp != k:
            w = np.concatenate([w, np.zeros((w.shape[0], kp - k), np.float32)], axis=1)
        assert name not in self.mats
        self.mats[name] = np.ascontiguousarray(w)
        return name

    def par(self, name, v):
        v = np.asarray(v, np.float32).reshape(-1)
        assert name not in self.params
        self.params[name] = np.ascontiguousarray(v)
        return name

    def act(self, name, rows, cols, view_of=None):
        assert name not in self.acts
        if view_of is not None:
            base = self.acts[view_of]
            assert base["rows"] * base["cols"] == rows * cols, name
        self.acts[name] = dict(rows=rows, cols=cols, view_of=view_of)
        return name

    # ---- ops ----
    def gemm(self, a, w, out, rows, k=None, b=None, oc=0, amap=None, omap=None, conv=None, kr=None, aprelu=False,
             act=0, res=None, rmap=None, res_mod=0, mask=0, tag=""):
        """out[r, oc + n] = mask(act(sum_k A[map(r), k] W[n, k] + bias[n] + res[rmap(r), n])).

        conv = (taps, dilation, upsample): A[r, tap * Cin + c] = a[(r + (tap - taps // 2) * dil) / up, c], zero
        outside the request's rows. kr = (k begin, k end) in the weight's K. act: 0 none, 1 ReLU, 2 LeakyReLU 0.01.
        mask = L / 16: rows at and past the request's token count times it are zeroed.
        """
        A = self.acts[a]
        if k is None:
            k = A["cols"] * (conv[0] if conv else 1)
        if kr is None:
            kr = (0, k)
        n = self.mats[w].shape[0]
        self.ops.append(dict(op="gemm", tag=tag, a=a, w=w, b=b, out=out, rows=rows, k=k, n=n, oc=oc,
                             amap=list(amap or (rows, 0, 0)), omap=list(omap or (rows, 0, 0)),
                             conv=list(conv) if conv else [0, 0, 0], kr=list(kr), aprelu=int(aprelu), act=act,
                             res=res, rmap=list(rmap or (rows, 0, 0)), res_mod=res_mod, mask=mask))

    def attn(self, qkv, out, rows, d, heads, mask_start=-1, tag=""):
        self.ops.append(dict(op="attn", tag=tag, qkv=qkv, out=out, rows=rows, d=d, heads=heads, mask_start=mask_start))

    def ln(self, x, g, b, rows, d, tag=""):
        self.ops.append(dict(op="ln", tag=tag, x=x, g=g, b=b, rows=rows, d=d))

    def glue(self, mode, tag="", **kw):
        self.ops.append(dict(op="glue", mode=mode, tag=tag, **kw))


def _conv_w(w):
    # [out, in, taps] -> [out, taps * in], tap-major.
    o, i, t = w.shape
    return np.transpose(w, (0, 2, 1)).reshape(o, t * i)


def _sincos(seq, dim):
    pe = np.zeros((seq, dim), np.float32)
    pos = np.arange(seq, dtype=np.float32)[:, None]
    div = np.exp(np.arange(0, dim, 2, dtype=np.float32) * -(np.log(10000.0) / dim)).astype(np.float32)
    pe[:, 0::2] = np.sin(pos * div)
    pe[:, 1::2] = np.cos(pos * div)
    return pe


def _transformer(B, st, prefix, name, nlayers, d, rows, s, qkv, att, ff, tmp, mask_start=-1):
    for l in range(nlayers):
        p = f"{prefix}.layers.{l}."
        n = f"{name}{l}"
        B.gemm(s, B.mat(n + ".in", st[p + "self_attn.in_proj_weight"]), qkv, rows,
               b=B.par(n + ".in.b", st[p + "self_attn.in_proj_bias"]), tag=n + " qkv")
        B.attn(qkv, att, rows, d, 16, mask_start=mask_start, tag=n + " attn")
        B.gemm(att, B.mat(n + ".out", st[p + "self_attn.out_proj.weight"]), tmp, rows,
               b=B.par(n + ".out.b", st[p + "self_attn.out_proj.bias"]), res=s, tag=n + " out")
        B.ln(tmp, B.par(n + ".n1.g", st[p + "norm1.weight"]), B.par(n + ".n1.b", st[p + "norm1.bias"]), rows, d,
             tag=n + " norm1")
        B.gemm(tmp, B.mat(n + ".ff1", st[p + "linear1.weight"]), ff, rows,
               b=B.par(n + ".ff1.b", st[p + "linear1.bias"]), act=1, tag=n + " ff1")
        B.gemm(ff, B.mat(n + ".ff2", st[p + "linear2.weight"]), s, rows,
               b=B.par(n + ".ff2.b", st[p + "linear2.bias"]), res=tmp, tag=n + " ff2")
        B.ln(s, B.par(n + ".n2.g", st[p + "norm2.weight"]), B.par(n + ".n2.b", st[p + "norm2.bias"]), rows, d,
             tag=n + " norm2")


def _decoder(B, st, p, name, x, out_feat, tcond, tcond_has, ext_views, d16, a16, d32, a32, d64, a64, ht, out,
             ext_kmap):
    """DoubleCondDecoder (down_t 2, width 512, depth 4, dilation 3, ReLU, no norm) on 16 positions.

    x: [16, Cin] masked input. tcond [64, Tc] with tcond_has [64, 1]. ext_views: per stage (act, k_logical) the
    external condition viewed per position; ext_kmap folds its weight columns (see callers).
    """
    W = lambda k: st[p + k + ".weight"]
    Bb = lambda k: st[p + k + ".bias"]
    B.gemm(x, B.mat(name + ".m0", _conv_w(W("model.0"))), d16, 16, b=B.par(name + ".m0.b", Bb("model.0")),
           conv=(3, 1, 1), act=1, tag=name + " conv0")
    stages = [(d16, a16, 16, 4), (d32, a32, 32, 2)]
    for i, (d, a, L, fpp) in enumerate(stages):
        # Target condition: per frame, a ReLU(Linear) of the target features replaces the hidden state's slice.
        tw = W(f"target_cond_blocks.{2 * i}")
        B.gemm(tcond, B.mat(f"{name}.t{i}", tw), ht[i], 64, b=B.par(f"{name}.t{i}.b", Bb(f"target_cond_blocks.{2 * i}")),
               act=1, tag=f"{name} target{i}")
        B.glue("replace", tag=f"{name} replace{i}", h=d + f".v{fpp}", src=ht[i], has=tcond_has, rows=64)
        # External condition: Linear(cat(h, cond)) + ReLU, as two GEMMs; masked (the residual stack's input).
        ew = W(f"external_cond_blocks.{2 * i}")
        eb = Bb(f"external_cond_blocks.{2 * i}")
        B.gemm(d, B.mat(f"{name}.e{i}h", ew[:, :512]), a, L, b=B.par(f"{name}.e{i}.b", eb), tag=f"{name} ext{i}h")
        ext_act, ext_k = ext_views[i]
        B.gemm(ext_act, B.mat(f"{name}.e{i}c", ext_kmap(i, ew[:, 512:])), d, L, k=ext_k, res=a, act=1,
               mask=L // 16, tag=f"{name} ext{i}c")
        # Resnet1D: dilations 27, 9, 3, 1.
        for j, dil in enumerate([27, 9, 3, 1]):
            blk = f"model.{2 + i}.0.model.{j}."
            kr = (512, 1024) if dil >= L else None
            B.gemm(d, B.mat(f"{name}.s{i}b{j}c1", _conv_w(W(blk + "conv1"))), a, L,
                   b=B.par(f"{name}.s{i}b{j}c1.b", Bb(blk + "conv1")), conv=(3, dil, 1), kr=kr, aprelu=True, act=1,
                   mask=L // 16, tag=f"{name} s{i} block{j} conv1")
            B.gemm(a, B.mat(f"{name}.s{i}b{j}c2", W(blk + "conv2")[:, :, 0]), d, L,
                   b=B.par(f"{name}.s{i}b{j}c2.b", Bb(blk + "conv2")), res=d, mask=L // 16,
                   tag=f"{name} s{i} block{j} conv2")
        # Upsample x2 (nearest) and conv3, masked.
        nxt = stages[i + 1][0] if i + 1 < len(stages) else d64
        B.gemm(d, B.mat(f"{name}.s{i}up", _conv_w(W(f"model.{2 + i}.2"))), nxt, 2 * L,
               b=B.par(f"{name}.s{i}up.b", Bb(f"model.{2 + i}.2")), conv=(3, 1, 2), mask=2 * L // 16,
               tag=f"{name} s{i} upsample conv")
    B.gemm(d64, B.mat(name + ".m4", _conv_w(W("model.4"))), a64, 64, b=B.par(name + ".m4.b", Bb("model.4")),
           conv=(3, 1, 1), act=1, mask=4, tag=name + " conv4")
    B.gemm(a64, B.mat(name + ".m6", _conv_w(W("model.6"))), out, 64, b=B.par(name + ".m6.b", Bb("model.6")),
           conv=(3, 1, 1), mask=4, tag=name + " conv6")


def build(pose_st, root_st, vq_st, mean, std, rig, weight_type="f16"):
    """Build the plan from the three checkpoint states (name -> float32 array, prefixes kept)."""
    B = Builder(weight_type)
    rs = {k[len("mmm_net."):]: v for k, v in root_st.items() if k.startswith("mmm_net.")}
    ps = {k[len("mmm_net."):]: v for k, v in pose_st.items() if k.startswith("mmm_net.")}
    vs = {k[len("pose_net."):]: v for k, v in vq_st.items() if k.startswith("pose_net.")}

    # ---- activations (rows per request, columns) ----
    B.act("in.grv", 8, 5)
    B.act("in.lrv", 8, 4)
    B.act("in.lp", 8, 268)
    B.act("in.flags", 8, 4)  # has global root, has local root, has pose, has frame (their max)
    B.act("in.allowed", 1, 12)
    B.act("meta.T", 1, 4)  # token count (float), 0, 0, 0
    B.act("r.emb", 8, 384)
    B.act("r.h0", 8, 512)
    B.act("r.h1", 8, 512)
    B.act("r.fe", 8, 512)
    B.act("r.pf", 8, 512)
    B.act("r.s1", 9, 512)
    B.act("r.q1", 9, 1536)
    B.act("r.a1", 9, 512)
    B.act("r.f1", 9, 2048)
    B.act("r.t1", 9, 512)
    B.act("r.logit", 1, 12)
    B.act("r.s2", 25, 512)
    B.act("r.q2", 25, 1536)
    B.act("r.a2", 25, 512)
    B.act("r.f2", 25, 2048)
    B.act("r.t2", 25, 512)
    B.act("r.x", 16, 512)
    B.act("r.df", 64, 512)
    B.act("r.df.v4", 16, 2048, view_of="r.df")
    B.act("r.df.v2", 32, 1024, view_of="r.df")
    B.act("r.dg", 64, 5)
    B.act("r.dh", 64, 1)
    # Decoder state, shared by the root decoder and the VQ-VAE decoder (they run one after the other).
    B.act("d16", 16, 512)
    B.act("d16.v4", 64, 128, view_of="d16")
    B.act("a16", 16, 512)
    B.act("d32", 32, 512)
    B.act("d32.v2", 64, 256, view_of="d32")
    B.act("a32", 32, 512)
    B.act("d64", 64, 512)
    B.act("a64", 64, 512)
    B.act("ht0", 64, 128)
    B.act("ht1", 64, 256)
    B.act("r.out", 64, 5)
    B.act("r.out.v4", 16, 20, view_of="r.out")
    B.act("c.lrv", 64, 4)
    B.act("c.lrv.v4", 16, 16, view_of="c.lrv")
    B.act("c.lrv.v2", 32, 8, view_of="c.lrv")
    B.act("p.pc", 64, 268)
    B.act("p.ph", 64, 1)
    B.act("p.pce", 64, 160)
    B.act("p.pe", 64, 160)
    B.act("p.pe.v", 16, 640, view_of="p.pe")
    B.act("p.s", 16, 1024)
    B.act("p.q", 16, 3072)
    B.act("p.a", 16, 1024)
    B.act("p.f", 16, 2048)
    B.act("p.t", 16, 1024)
    B.act("p.logit", 16, 80)
    B.act("v.x", 16, 160)
    B.act("v.out", 64, 365)

    # ================= root model =================
    B.gemm("in.lp", B.mat("r.plp", rs["_proj_local_pose.weight"]), "r.emb", 8, b=B.par("r.plp.b", rs["_proj_local_pose.bias"]),
           oc=0, tag="root proj pose")
    B.gemm("in.lrv", B.mat("r.plr", rs["_proj_local_root_value.weight"]), "r.emb", 8,
           b=B.par("r.plr.b", rs["_proj_local_root_value.bias"]), oc=256, tag="root proj local root")
    B.gemm("in.grv", B.mat("r.pgr", rs["_proj_global_root_value.weight"]), "r.emb", 8,
           b=B.par("r.pgr.b", rs["_proj_global_root_value.bias"]), oc=320, tag="root proj global root")
    B.par("r.no", np.concatenate([rs["_no_local_pose_emb"], rs["_no_local_root_emb"], rs["_no_global_root_emb"]]))
    B.glue("root_select", tag="root no-embeddings", emb="r.emb", flags="in.flags", no="r.no")
    for half, nm, off in (("start", "_proj_start_input", 0), ("end", "_proj_end_input", 4)):
        mp = (4, 8, off)
        B.gemm("r.emb", B.mat(f"r.{half}0", rs[nm + ".fc_layers.0.weight"]), "r.h0", 4, b=B.par(f"r.{half}0.b", rs[nm + ".fc_layers.0.bias"]),
               amap=mp, omap=mp, act=2, tag=f"root {half} mlp 0")
        B.gemm("r.h0", B.mat(f"r.{half}1", rs[nm + ".fc_layers.1.weight"]), "r.h1", 4, b=B.par(f"r.{half}1.b", rs[nm + ".fc_layers.1.bias"]),
               amap=mp, omap=mp, act=2, tag=f"root {half} mlp 1")
        B.gemm("r.h1", B.mat(f"r.{half}2", rs[nm + ".forward_projection.weight"]), "r.fe", 4,
               b=B.par(f"r.{half}2.b", rs[nm + ".forward_projection.bias"]), amap=mp, omap=mp, tag=f"root {half} mlp out")
    B.par("r.inpos", rs["_input_position_emb.weight"])
    B.par("r.ntok", rs["_proj_input_num_tokens.weight"][12])  # num_tokens is always the masked class (18 - 6)
    B.glue("seq1", tag="root sequence 1", fe="r.fe", pf="r.pf", s="r.s1", inpos="r.inpos", ntok="r.ntok")
    _transformer(B, rs, "_shared_transformer_model", "r.L", 6, 512, 9, "r.s1", "r.q1", "r.a1", "r.f1", "r.t1")
    B.gemm("r.s1", B.mat("r.ntl", rs["_proj_num_token_output_logit.weight"]), "r.logit", 1,
           b=B.par("r.ntl.b", rs["_proj_num_token_output_logit.bias"]), amap=(1, 9, 0), tag="root token-count logits")
    B.glue("ntok_argmax", tag="root token count", logit="r.logit", allowed="in.allowed", T="meta.T")
    B.par("r.mid", rs["_middle_token_emb.weight"])
    B.par("r.pos", rs["_position_emb.embed"][:, 0, :])
    B.glue("seq2", tag="root sequence 2", pf="r.pf", s="r.s2", mid="r.mid", pos="r.pos", T="meta.T")
    _transformer(B, rs, "_root_token_transformer_model", "r.M", 2, 512, 25, "r.s2", "r.q2", "r.a2", "r.f2", "r.t2",
                 mask_start=9)
    B.glue("mask_copy", tag="root decoder input", src="r.s2", dst="r.x", rows=16, src_off=9, mask=1, T="meta.T")
    B.par("r.nofr", rs["_conv_no_frame_emb"])
    B.glue("root_dense", tag="root dense conditions", fe="r.fe", flags="in.flags", grv="in.grv", nofr="r.nofr",
           df="r.df", dg="r.dg", dh="r.dh", T="meta.T")
    rd = "_conv_output."
    _decoder(B, {k[len(rd):]: v for k, v in rs.items() if k.startswith(rd)}, "", "r.D", "r.x", 5, "r.dg", "r.dh",
             [("r.df.v4", 2048), ("r.df.v2", 1024)], "d16", "a16", "d32", "a32", "d64", "a64", ["ht0", "ht1"],
             "r.out", lambda i, w: w)
    # ================= root -> local =================
    root_mean = mean[DUAL_GLOBAL_REP][:5]
    root_std = np.sqrt(std[DUAL_GLOBAL_REP][:5] ** 2 + EPS_STATS)
    loc_mean = mean[DUAL_LOCAL_REP][:4]
    loc_std = np.sqrt(std[DUAL_LOCAL_REP][:4] ** 2 + EPS_STATS)
    B.par("c.stats", np.concatenate([root_mean, root_std, loc_mean, loc_std]))
    B.glue("root_to_local", tag="root to local", src="r.out", lrv="in.lrv", flags="in.flags", dst="c.lrv",
           stats="c.stats", T="meta.T")
    # ================= pose model =================
    B.glue("pose_dense", tag="pose dense conditions", lp="in.lp", flags="in.flags", pc="p.pc", ph="p.ph", T="meta.T")
    B.gemm("p.pc", B.mat("p.plp", ps["_proj_local_pose.weight"]), "p.pce", 64, b=B.par("p.plp.b", ps["_proj_local_pose.bias"]),
           tag="pose proj condition")
    # The pose tokens are all the mask token (one inference step): their embedding is a constant.
    emb = ps["_pose_token_emb.weight"]
    mask_rows = emb[[16 + 17 * h for h in range(5)]].reshape(-1)  # [160]
    hdn = mask_rows
    for l in range(2):
        hdn = hdn @ ps[f"_proj_pose_token_emb.fc_layers.{l}.weight"].T + ps[f"_proj_pose_token_emb.fc_layers.{l}.bias"]
        hdn = np.where(hdn >= 0, hdn, 0.01 * hdn)
    hdn = hdn @ ps["_proj_pose_token_emb.forward_projection.weight"].T + ps["_proj_pose_token_emb.forward_projection.bias"]
    B.par("p.mask_emb", hdn.astype(np.float32))  # [640] = 4 frames x 160
    B.glue("pose_select", tag="pose embeddings", pce="p.pce", ph="p.ph", dst="p.pe", c="p.mask_emb")
    wi = ps["_proj_input.0.weight"]
    bi = ps["_proj_input.0.bias"]
    B.gemm("p.pe.v", B.mat("p.in_p", wi[:, :640]), "p.s", 16, b=B.par("p.in.b", bi), tag="pose input (pose)")
    # Root condition: global root [x, z, cos, sin] of 4 frames -> Linear(16, 256) -> the input projection's columns;
    # folded into one 20-column weight over the decoder's 5 features (height zeroed).
    wr = ps["_proj_local_root_values.weight"]  # [256, 16]
    br = ps["_proj_local_root_values.bias"]
    wfold = wi[:, 640:896] @ wr  # [1024, 16]
    w20 = np.zeros((1024, 20), np.float32)
    for f in range(4):
        for j, c in enumerate([0, 2, 3, 4]):
            w20[:, f * 5 + c] = wfold[:, f * 4 + j]
    B.gemm("r.out.v4", B.mat("p.in_r", w20), "p.s", 16, b=B.par("p.in_r.b", wi[:, 640:896] @ br), res="p.s",
           tag="pose input (root)")
    B.par("p.ntok", ps["_proj_num_valid_positions.weight"] @ wi[:, 896:1024].T)  # [11, 1024]
    B.par("p.pos", ps["_position_emb.embed"][:, 0, :])
    B.glue("pose_in", tag="pose input", s="p.s", ntok="p.ntok", pos="p.pos", T="meta.T")
    _transformer(B, ps, "_transformer_model", "p.L", 16, 1024, 16, "p.s", "p.q", "p.a", "p.f", "p.t", mask_start=0)
    B.gemm("p.s", B.mat("p.logit", ps["_proj_pose_output_logit.weight"]), "p.logit", 16,
           b=B.par("p.logit.b", ps["_proj_pose_output_logit.bias"]), tag="pose logits")
    B.par("v.codebook", vs["quantizer.vq._codebook.embed"])  # [5, 16, 32]
    B.glue("pose_tokens", tag="pose tokens", logit="p.logit", dst="v.x", codebook="v.codebook", T="meta.T")
    # ================= VQ-VAE decoder =================

    def vq_ext(i, w):
        # External condition: local root velocity (2 of the 4 local root features) per frame.
        fpp = 4 if i == 0 else 2
        out = np.zeros((512, fpp * 4), np.float32)
        for f in range(fpp):
            out[:, f * 4 + 1] = w[:, f * 2]
            out[:, f * 4 + 2] = w[:, f * 2 + 1]
        return out

    vd = "decoder."
    _decoder(B, {k[len(vd):]: v for k, v in vs.items() if k.startswith(vd)}, "", "v.D", "v.x", 365, "p.pc", "p.ph",
             [("c.lrv.v4", 16), ("c.lrv.v2", 8)], "d16", "a16", "d32", "a32", "d64", "a64", ["ht0", "ht1"], "v.out",
             vq_ext)

    meta = dict(
        model="motionbricks-soma-v6", fps=FPS, frames_per_token=4, min_tokens=6, max_tokens=16,
        mean=mean.astype(np.float32).tolist(), std=std.astype(np.float32).tolist(),
        capture_offsets=rig["capture_offsets"].astype(np.float32).reshape(-1).tolist(),
        standard_offsets=rig["standard_offsets"].astype(np.float32).reshape(-1).tolist(),
    )
    return B, meta


# ---------------------------------------------------------------------------------------------------------------
# .fmb file: "FMB1", u32 version, u32 json bytes, json, zero padding to 256, then the data sections (256-aligned).


def _f16(w):
    return w.astype(np.float16)


def pack(B, meta, chunk_bytes=96 << 20):
    wt = B.weight_type
    esize = 2 if wt == "f16" else 4
    chunks = []
    cur = []
    cur_bytes = 0
    mats = {}
    for name, w in B.mats.items():
        nb = w.size * esize
        if cur and cur_bytes + nb > chunk_bytes:
            chunks.append(cur)
            cur, cur_bytes = [], 0
        mats[name] = dict(chunk=len(chunks), offset=cur_bytes // esize, rows=w.shape[0], cols=w.shape[1])
        cur.append(w)
        cur_bytes += _roundup(nb, 256)
        if nb % 256:
            cur.append(np.zeros((_roundup(nb, 256) - nb) // esize, np.float32))
    if cur:
        chunks.append(cur)
    params = {}
    pblob = []
    poff = 0
    for name, v in B.params.items():
        params[name] = dict(offset=poff, size=v.size)
        n = _roundup(v.size, 4)
        pblob.append(np.concatenate([v, np.zeros(n - v.size, np.float32)]))
        poff += n
    sections = []
    for ch in chunks:
        arr = np.concatenate([c.reshape(-1) for c in ch])
        sections.append((_f16(arr) if wt == "f16" else arr.astype(np.float32)).tobytes())
    sections.append(np.concatenate(pblob).astype(np.float32).tobytes())
    head = dict(version=VERSION, weight_type=wt, meta=meta, acts=B.acts, mats=mats, params=params, ops=B.ops,
                sections=[])
    # Two passes: the section offsets depend on the header's size.
    for _ in range(2):
        js = json.dumps(head, separators=(",", ":")).encode()
        off = _roundup(12 + len(js), 256)
        head["sections"] = []
        for s in sections:
            head["sections"].append(dict(offset=off, bytes=len(s)))
            off = _roundup(off + len(s), 256)
    js = json.dumps(head, separators=(",", ":")).encode()
    assert _roundup(12 + len(js), 256) <= head["sections"][0]["offset"]
    out = bytearray(MAGIC + struct.pack("<II", VERSION, len(js)) + js)
    for s, sec in zip(sections, head["sections"]):
        out += b"\0" * (sec["offset"] - len(out))
        out += s
    return bytes(out)


def load(path):
    data = open(path, "rb").read()
    assert data[:4] == MAGIC
    _, n = struct.unpack("<II", data[4:12])
    head = json.loads(data[12:12 + n])
    secs = head["sections"]
    dt = np.float16 if head["weight_type"] == "f16" else np.float32
    chunks = [np.frombuffer(data, dt, s["bytes"] // np.dtype(dt).itemsize, s["offset"]).astype(np.float32)
              for s in secs[:-1]]
    pbuf = np.frombuffer(data, np.float32, secs[-1]["bytes"] // 4, secs[-1]["offset"])
    W = {k: chunks[m["chunk"]][m["offset"]:m["offset"] + m["rows"] * m["cols"]].reshape(m["rows"], m["cols"])
         for k, m in head["mats"].items()}
    P = {k: pbuf[p["offset"]:p["offset"] + p["size"]] for k, p in head["params"].items()}
    return head, W, P


# ---------------------------------------------------------------------------------------------------------------
# NumPy executor of the plan: float32, op for op as the GPU.


class Acts:
    def __init__(self, head, N):
        self.N = N
        self.head = head
        self.buf = {}
        for k, a in head["acts"].items():
            if a["view_of"] is None:
                self.buf[k] = np.zeros((N, a["rows"], a["cols"]), np.float32)
        for k, a in head["acts"].items():
            if a["view_of"] is not None:
                self.buf[k] = self.buf[a["view_of"]].reshape(N, a["rows"], a["cols"])

    def __getitem__(self, k):
        return self.buf[k]


def _rowmap(r, m):
    g, s, o = m
    return (r // g) * s + o + r % g


def _tokens(A):
    return A["meta.T"][:, 0, 0].astype(np.int64)


def run(head, W, P, A, stop_after=None):
    N = A.N
    T = None
    for op in head["ops"]:
        k = op["op"]
        if k == "gemm":
            _gemm(op, W, P, A)
        elif k == "attn":
            _attn(op, A)
        elif k == "ln":
            x = A[op["x"]]
            mu = x.mean(-1, keepdims=True)
            var = ((x - mu) ** 2).mean(-1, keepdims=True)
            x[...] = (x - mu) / np.sqrt(var + 1e-5) * P[op["g"]] + P[op["b"]]
        else:
            _glue(op, P, A)
        if stop_after is not None and op["tag"] == stop_after:
            return


def _gemm(op, W, P, A):
    N = A.N
    a = A[op["a"]]
    w = W[op["w"]]
    rows = op["rows"]
    r = np.arange(rows)
    taps, dil, up = op["conv"]
    kb, ke = op["kr"]
    if taps:
        cin = a.shape[2]
        Lin = a.shape[1]
        cols = []
        for tap in range(taps):
            t = r + (tap - taps // 2) * dil
            valid = (t >= 0) & (t < rows)
            src = np.clip(t, 0, rows - 1) // up
            g = a[:, src, :] * valid[None, :, None]
            cols.append(g)
        X = np.concatenate(cols, axis=2)  # [N, rows, taps*cin]
    else:
        X = a[:, _rowmap(r, op["amap"]), :]
    if op["aprelu"]:
        X = np.maximum(X, 0)
    X = X[:, :, kb:ke]
    if HALF_A:
        X = np.clip(X, -65504.0, 65504.0).astype(np.float16).astype(np.float32)
    acc = X @ w[:, kb:ke].T
    if op["b"] is not None:
        acc = acc + P[op["b"]]
    if op["res"] is not None:
        rm = _rowmap(r, op["rmap"])
        R = A[op["res"]][:, rm, :]
        acc = acc + R
    if op["act"] == 1:
        acc = np.maximum(acc, 0)
    elif op["act"] == 2:
        acc = np.where(acc >= 0, acc, 0.01 * acc)
    if op["mask"]:
        T = _tokens(A)
        m = (r[None, :] < (T[:, None] * op["mask"])).astype(np.float32)
        acc = acc * m[:, :, None]
    o = A[op["out"]]
    om = _rowmap(r, op["omap"])
    o[:, om, op["oc"]:op["oc"] + op["n"]] = acc


def _attn(op, A):
    qkv = A[op["qkv"]]
    d, H = op["d"], op["heads"]
    hd = d // H
    S = op["rows"]
    q = qkv[:, :, :d].reshape(A.N, S, H, hd).transpose(0, 2, 1, 3)
    kk = qkv[:, :, d:2 * d].reshape(A.N, S, H, hd).transpose(0, 2, 1, 3)
    v = qkv[:, :, 2 * d:].reshape(A.N, S, H, hd).transpose(0, 2, 1, 3)
    s = (q / np.sqrt(np.float32(hd))) @ kk.transpose(0, 1, 3, 2)  # [N, H, S, S]
    if op["mask_start"] >= 0:
        T = _tokens(A)
        j = np.arange(S)
        valid = (j[None, :] < op["mask_start"]) | ((j[None, :] - op["mask_start"]) < T[:, None])
        s = np.where(valid[:, None, None, :], s, -np.inf)
    s = s - s.max(-1, keepdims=True)
    e = np.exp(s)
    p = e / e.sum(-1, keepdims=True)
    o = (p @ v).transpose(0, 2, 1, 3).reshape(A.N, S, d)
    A[op["out"]][...] = o


def _glue(op, P, A):
    m = op["mode"]
    N = A.N
    if m == "root_select":
        e = A[op["emb"]]
        f = A[op["flags"]]
        no = P[op["no"]]
        for c0, c1, fl in ((0, 256, 2), (256, 320, 1), (320, 384, 0)):
            keep = f[:, :, fl:fl + 1] > 0.5
            e[:, :, c0:c1] = np.where(keep, e[:, :, c0:c1], no[c0:c1])
    elif m == "seq1":
        pf = A[op["pf"]]
        pf[...] = A[op["fe"]] + P[op["inpos"]].reshape(8, 512)
        s = A[op["s"]]
        s[:, 0, :] = P[op["ntok"]]
        s[:, 1:, :] = pf
    elif m == "ntok_argmax":
        lg = A[op["logit"]][:, 0, :].copy()
        al = A[op["allowed"]][:, 0, :]
        lg[:, 11] = -np.inf
        lg[:, :11] = np.where(al[:, :11] > 0.5, lg[:, :11], -np.inf)
        A[op["T"]][:, 0, 0] = np.argmax(lg, -1) + 6
    elif m == "seq2":
        T = _tokens(A)
        s = A[op["s"]]
        s[:, 0, :] = P[op["mid"]].reshape(12, 512)[T - 6]
        s[:, 1:9, :] = A[op["pf"]]
        s[:, 9:, :] = P[op["pos"]].reshape(16, 512)
    elif m == "mask_copy":
        T = _tokens(A)
        rows = op["rows"]
        src = A[op["src"]][:, op["src_off"]:op["src_off"] + rows, :]
        msk = (np.arange(rows)[None, :] < T[:, None] * op["mask"]).astype(np.float32)
        A[op["dst"]][...] = src * msk[:, :, None]
    elif m == "root_dense":
        T = _tokens(A)
        fe = A[op["fe"]]
        fl = A[op["flags"]]
        grv = A[op["grv"]]
        nofr = P[op["nofr"]]
        df, dg, dh = A[op["df"]], A[op["dg"]], A[op["dh"]]
        df[...] = nofr
        dg[...] = 0
        dh[...] = 0
        for b in range(N):
            for slot in range(8):
                fr = slot if slot < 4 else (T[b] - 1) * 4 + slot - 4
                df[b, fr] = fe[b, slot] if fl[b, slot, 3] > 0.5 else nofr
                dg[b, fr] = grv[b, slot]
                dh[b, fr, 0] = fl[b, slot, 0]
    elif m == "replace":
        h = A[op["h"]]
        h[...] = np.where(A[op["has"]] > 0.5, A[op["src"]], h)
    elif m == "root_to_local":
        T = _tokens(A)
        st = P[op["stats"]]
        rm, rsd, lm, lsd = st[0:5], st[5:10], st[10:14], st[14:18]
        g = A[op["src"]] * rsd + rm  # [N, 64, 5]
        ang = np.arctan2(g[..., 4], g[..., 3])
        # The reference's quaternion round trip: atan2(sin(a/2), cos(a/2)) * 2.
        ang = np.arctan2(np.sin(ang / 2), np.cos(ang / 2)) * 2
        c, s = np.cos(ang), np.sin(ang)
        av = FPS * np.arctan2(s[:, 1:] * c[:, :-1] - c[:, 1:] * s[:, :-1], c[:, 1:] * c[:, :-1] + s[:, 1:] * s[:, :-1])
        av = np.concatenate([av, np.zeros((N, 1), np.float32)], 1)
        pos = g[..., 0:3]
        vel = FPS * (pos[:, 1:] - pos[:, :-1])
        vel = np.concatenate([vel, np.zeros((N, 1, 3), np.float32)], 1)
        for b in range(N):
            last = T[b] * 4 - 1
            av[b, last] = av[b, last - 1]
            vel[b, last] = vel[b, last - 1]
        loc = np.stack([av, vel[..., 0], vel[..., 2], pos[..., 1]], -1)
        out = (loc - lm) / lsd
        fl = A[op["flags"]]
        lrv = A[op["lrv"]]
        for b in range(N):
            if fl[b, 7, 1] > 0.5:
                out[b, T[b] * 4 - 1] = lrv[b, 7]
        A[op["dst"]][...] = out
    elif m == "pose_dense":
        T = _tokens(A)
        lp = A[op["lp"]]
        fl = A[op["flags"]]
        pc, ph = A[op["pc"]], A[op["ph"]]
        pc[...] = 0
        ph[...] = 0
        for b in range(N):
            for slot in range(8):
                fr = slot if slot < 4 else (T[b] - 1) * 4 + slot - 4
                pc[b, fr] = lp[b, slot]
                ph[b, fr, 0] = fl[b, slot, 2]
    elif m == "pose_select":
        c = P[op["c"]].reshape(4, 160)
        ph = A[op["ph"]]
        fr = np.arange(64) % 4
        A[op["dst"]][...] = np.where(ph > 0.5, A[op["pce"]], c[fr][None])
    elif m == "pose_in":
        T = _tokens(A)
        s = A[op["s"]]
        s[...] = np.maximum(s + P[op["ntok"]].reshape(11, 1024)[T - 6][:, None, :], 0) + P[op["pos"]].reshape(16, 1024)
    elif m == "pose_tokens":
        T = _tokens(A)
        lg = A[op["logit"]].reshape(N, 16, 5, 16)
        tok = np.argmax(lg, -1)  # [N, 16, 5]
        cb = P[op["codebook"]].reshape(5, 16, 32)
        x = np.stack([cb[h][tok[:, :, h]] for h in range(5)], 2).reshape(N, 16, 160)
        msk = (np.arange(16)[None, :] < T[:, None]).astype(np.float32)
        A[op["dst"]][...] = x * msk[:, :, None]
        A.tokens = tok
    else:
        raise ValueError(m)


# ---------------------------------------------------------------------------------------------------------------
# The model's own input preparation and output decoding (CPU in the engine as well).


def cont6d_to_matrix(c):
    x = c[..., 0:3] / np.linalg.norm(c[..., 0:3], axis=-1, keepdims=True)
    z = np.cross(x, c[..., 3:6])
    z = z / np.linalg.norm(z, axis=-1, keepdims=True)
    y = np.cross(z, x)
    return np.stack([x, y, z], -1)


def matrix_to_cont6d(m):
    return np.concatenate([m[..., 0], m[..., 1]], -1)


def prepare(meta, inputs, A):
    """ONNX-style inputs (global_root_values [N,8,5], has_* [N,8], local_root_values [N,8,4], local_poses [N,8,267],
    allowed_pred_num_tokens [N,11]) -> the plan's input activations. Returns the values the decoding needs."""
    mean = np.array(meta["mean"], np.float32)
    std = np.array(meta["std"], np.float32)
    cap = np.array(meta["capture_offsets"], np.float32).reshape(30, 3, 3)
    sta = np.array(meta["standard_offsets"], np.float32).reshape(30, 3, 3)
    grv, hg, lrv, hl, lp, hp, allowed = [np.asarray(inputs[k], np.float32) for k in (
        "global_root_values", "has_global_root_values", "local_root_values", "has_local_root_values", "local_poses",
        "has_local_poses", "allowed_pred_num_tokens")]
    N = grv.shape[0]
    lp = lp.copy()
    rot = cont6d_to_matrix(lp[:, :, 87:].reshape(N, 8, 30, 6))
    rot = rot @ cap[None, None] @ np.transpose(sta, (0, 2, 1))[None, None]
    lp[:, :, 87:] = matrix_to_cont6d(rot).reshape(N, 8, 180)
    off = grv[:, :1, [0, 2]].copy()
    head0 = np.arctan2(grv[:, 0, 4], grv[:, 0, 3])
    g = grv.copy()
    g[:, :, [0, 2]] -= off
    gm, gs = mean[DUAL_GLOBAL_REP], np.sqrt(std[DUAL_GLOBAL_REP] ** 2 + EPS_STATS)
    lm, ls = mean[DUAL_LOCAL_REP], np.sqrt(std[DUAL_LOCAL_REP] ** 2 + EPS_STATS)
    A["in.grv"][...] = (g - gm[:5]) / gs[:5]
    A["in.lrv"][...] = (lrv - lm[:4]) / ls[:4]
    A["in.lp"][...] = (np.concatenate([g[:, :, 1:2], lp], -1) - lm[LOCAL_POSE_FEAT]) / ls[LOCAL_POSE_FEAT]
    A["in.flags"][...] = np.stack([hg, hl, hp, np.maximum(np.maximum(hg, hl), hp)], -1)
    A["in.allowed"][:, 0, :11] = allowed
    return dict(off=off, head0=head0, grv_n=A["in.grv"].copy(), has_global=hg)


def decode(meta, A, ctx):
    """The plan's outputs -> pred_global_motions [N, 64, 366], pred_num_tokens [N]."""
    mean = np.array(meta["mean"], np.float32)
    std = np.array(meta["std"], np.float32)
    cap = np.array(meta["capture_offsets"], np.float32).reshape(30, 3, 3)
    sta = np.array(meta["standard_offsets"], np.float32).reshape(30, 3, 3)
    N = A.N
    T = _tokens(A)
    lm, ls = mean[DUAL_LOCAL_REP], np.sqrt(std[DUAL_LOCAL_REP] ** 2 + EPS_STATS)
    gm, gs = mean[DUAL_GLOBAL_REP], np.sqrt(std[DUAL_GLOBAL_REP] ** 2 + EPS_STATS)
    loc = A["v.out"] * ls + lm  # [N, 64, 365]
    rv = loc[..., 0] / FPS
    vel = loc[..., 1:3] / FPS
    ang = np.zeros_like(rv)
    ang[:, 1:] = np.cumsum(rv[:, :-1], 1)
    q0, q2 = np.cos(ang / 2), np.sin(ang / 2)
    pos = np.zeros((N, 64, 3), np.float32)
    pos[:, 1:, 0] = np.cumsum(vel[:, :-1, 0], 1)
    pos[:, 1:, 2] = np.cumsum(vel[:, :-1, 1], 1)
    pos[..., 1] = loc[..., 3]
    a2 = np.arctan2(q2, q0) * 2
    out = np.concatenate([pos, np.cos(a2)[..., None], np.sin(a2)[..., None], loc[..., 4:]], -1)
    # Root from the root module where the pose module misses the target height.
    groot = A["r.out"] * gs[:5] + gm[:5]
    tgt_h = (ctx["grv_n"][:, 4:8, 1] * gs[1] + gm[1])
    w = ctx["has_global"][:, 4:8]
    for b in range(N):
        fr = (T[b] - 1) * 4 + np.arange(4)
        e_pose = (np.abs(out[b, fr, 1] - tgt_h[b]) * w[b]).sum() / w[b].sum()
        e_root = (np.abs(groot[b, fr, 1] - tgt_h[b]) * w[b]).sum() / w[b].sum()
        if e_root < e_pose and e_pose > 0.05:
            out[b, :, 0:5] = groot[b]
    ang = np.arctan2(out[..., 4], out[..., 3]) + ctx["head0"][:, None]
    out[..., 3] = np.cos(ang)
    out[..., 4] = np.sin(ang)
    out[..., [0, 2]] += ctx["off"]
    rot = cont6d_to_matrix(out[..., 92:272].reshape(N, 64, 30, 6))
    rot = rot @ sta[None, None] @ np.transpose(cap, (0, 2, 1))[None, None]
    out[..., 92:272] = matrix_to_cont6d(rot).reshape(N, 64, 180)
    return out, T
