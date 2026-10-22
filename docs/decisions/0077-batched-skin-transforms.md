# 0077 — A skin's bone transforms handed to the RenderingServer in one call

Status: **KEEP**, on by default from `fellow170` · Branch `fellow/geometry` · Setting `rendering/fellow/animation/batched_skin_transforms` ·
Code: `scene/3d/skeleton_3d.*`, `servers/rendering/rendering_server.h`, `rendering_server_default.h`,
`storage/mesh_storage.h`, `renderer_rd/storage_rd/mesh_storage.*`, `fellow_settings.*`.

## Problem

After each pose update a Skeleton3D hands every bone's skinning transform (its global pose times the skin's bind
pose) to the RenderingServer with one `skeleton_bone_set_transform()` per bone: Crowd Lab's 256 skeletons of 52 bones
make 13 312 calls per frame on the main thread. Each goes through the server's wrapper (a redraw request, the caller
thread test, a check of the command queue) and then looks the skeleton up in its RID owner, which is thread-safe: a
mutex per call. The data it writes is 12 floats.

## Change

`RenderingServer::fellow_skeleton_set_bone_transforms(RID, const Vector<Transform3D> &)` (not bound): the transforms of
bones 0..size − 1 in one call. Its default implementation (the base server, the dummy and GL Compatibility storages)
calls `skeleton_bone_set_transform()` for each; the RenderingDevice storage looks the skeleton up once, writes the
same 12 floats per bone in the same layout and marks the skeleton dirty once (marking is idempotent). With the setting,
Skeleton3D computes the same transforms into a vector it keeps and makes the one call; where a bind's bone is missing it
takes stock's loop (and its errors).

## Verification

lavapipe, captures, demote diagnostics, autotune off, the setting off against on: Crowd Lab (3 cameras) and City Lab
(3; no skeletons): **0 differing pixels**.

RTX, `fellow169` (`m15_f227`): the warm `crowd_pan` sequence off against on **bit for bit** (99 dB, max |d| 0). In one
process (blocks of 30 frames, autotune off), Crowd Lab's frame:

| thread model | off | on | |
|---|---:|---:|---:|
| default ("Safe": the RenderingServer called directly) | 4.355 ms | 4.344 | −0.3 % (n.s.) |
| separate render thread (each call a command in the queue) | 4.634 | **3.466** | **−25.2 %** (p 4e-144) |

With the server called directly a call per bone costs little on this machine. With the separate render thread each
call is a command pushed into the render thread's queue by the main thread: 13 312 per frame were a quarter of Crowd
Lab's frame. With one command per skin the separate thread now runs Crowd Lab faster than the default model (3.47
against 4.34 ms), where before it was slower (4.63).

## Decision

**KEEP, on by default from `fellow170`.** The same transforms in the server's data; nothing lost with the default
thread model; with the separate render thread, which `docs/FELLOWENGINE_OPTIONS.md` recommends to projects whose main
thread works, animated characters cost a quarter less.
