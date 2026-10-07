# 0022 — Persistent instance data

Status: **MEASURING** · Branch `fellow/geometry` · Toggle `rendering/fellow/cpu/persistent_instances` (runtime, default
off until measured) · Code: `render_forward_clustered.{h,cpp}` (`FellowPersistent`, `_fellow_persistent_*`,
`_fellow_slot_*`, the three instance data writers, `_setup_render_pass_uniform_set`), `scene_shader_forward_clustered.*`
(specialization bit `fellow_persistent_instances`, binding 39 dynamic), `shaders/forward_clustered/scene_forward_clustered*.glsl`
(`fellow_element_init()`, `FELLOW_INSTANCE()`), `shaders/effects/fellow_instance_scatter.glsl`, `fellow_settings.*`.

## Problem

Every frame the forward renderer writes, for every element of every render list (the camera's lists and every shadow
pass), stock's 176-byte instance data: the instance's transform and previous transform, the surface's compressed AABB
and UV scale, the instance uniforms offset, layer mask, lightmap UV scale, and the flags and GI offset the pass gave it.
It is built from the instance and its mesh surface (pointer chasing) and written into the per-frame buffer, device-local
memory the CPU writes over PCIe on a discrete GPU. In City Lab this costs 0.32 ms of the render thread's 1.56 ms
(`m4_f61_city_cull_ab`: opaque 0.17 ms, shadow lists 0.13 ms), although almost none of it changes from one frame to the
next: 300 of 19 115 instances move.

Writing it into system memory instead (decision 0021) halves the writers' time (−0.16 ms) but makes the GPU read it over
PCIe: in one process, City Lab render thread −4.3 %, GPU time +2.6 % (pre-pass +0.022 ms, opaque +0.015 ms), frame
+0.9 %; draw-heavy City Lab render thread −7.1 %, GPU +3.4 %, frame −2.2 % (`m4_f67_*_mem_ab`).

## Change

* Every surface cache (an instance's surface) gets an entry (slot) in a device-local buffer kept from frame to frame,
  holding stock's InstanceData without the two fields a pass sets (flags, GI offset). A copy of each entry is kept on
  the CPU.
* The per-frame instance buffers hold one 16-byte element per list element instead: the slot, the flags and the GI
  offset that element's pass gave it (exactly what stock would write there).
* The persistent buffer has one region per frame in flight (a dynamic persistent buffer, like stock's per-frame
  buffers: the CPU writes the region of the frame being recorded while the GPU may still read the others). The frame's
  first list fill maps the next region and writes into it the entries written since that region was last mapped (the
  entries written by each of the last four maps are noted); a region not seen before (a new buffer) gets every valid
  entry.
* Every list element checks its surface's entry (any thread). The instance carries a version (`fellow_data_version`,
  changed by every setter of a field the entry holds and by the frame's update of the previous transform); if the entry
  was built from the current version it is current, and the check only reads (the surface's cache line stays shared
  between the threads of the different lists). Otherwise the first element of the frame that meets it (an atomic
  exchange of the frame number on the surface) builds the entry as stock builds the element's data and compares it
  with the CPU copy; a changed entry is copied and written into the frame's region (and noted for the next regions).
  (A new mesh, material or other change that makes new surface caches gives them new entries.)
* The scene shader (behind a specialization bit, so pipelines without it are unchanged) reads the element in the vertex
  and fragment stages (`fellow_element_init()`), then every other field from the entry (`FELLOW_INSTANCE()`); the flags
  and GI offset come from the element. Without the bit it reads the per-frame buffer as stock does.
* The buffer grows by doubling (a new buffer; its regions get every valid entry when first mapped). Slots are handed
  out when a surface cache is created and returned when it is freed.
* A tunable of autotune (decision 0020), measured by the frames' wall time: it moves work from the render thread to the
  GPU (the shaders read each element's entry through its slot), so it pays where the render thread is the limit.

History: the first version (`fellow66`) exchanged the frame number on every surface's first element of a frame (a write
of every surface's cache line, which the shadow lists' threads had just read) and uploaded the changed entries with a
buffer update and a scatter dispatch per list; the second (`fellow67`) only read in the common case and uploaded once
per frame (one dispatch before the first draws). The third (`fellow68`, above) writes the entries directly: no upload,
no dispatch, no barrier.

## Why it is exact

For every element the shader reads the same values stock writes into that element: the entry was built by stock's code
for the same surface in the same frame (the fields it holds do not change between the lists of a frame: transforms are
set before rendering, the previous transform is updated by the frame's first list fill, before any instance data is
built), and the flags and GI offset are the element's own. `cpu/verify` rebuilds stock's data for every element and
compares it with the entry as the GPU will read it (in the frame's region of the persistent buffer) and with the element
(`_fellow_verify_elements`).

## Verification

* lavapipe, with persistent instance data off: City Lab captures bit-identical to the previous build's (the shader
  changes leave the stock path's results as they were).
* lavapipe, off vs on with `cpu/verify` on the "on" side: City Lab captures (1.5 million elements, 26 887 entries
  uploaded, 548 checks), Lights Lab walk (60 frames), Stress Lab temporal (120 frames: moving instances, a
  vertex-animated surface; 5349 checks), PBR Lab (7 cameras): **0 differing pixels, 0 mismatches**.
* lavapipe, with the version check: City Lab walk (60 frames, the carts moving; 34 421 entries uploaded, 559 checks):
  **0 differing pixels, 0 mismatches**.
* lavapipe with the Khronos validation layer, async compute created at start-up, persistent instance data and autotune
  evaluating every 20 frames: City Lab captures: 0 differing pixels against async compute off, no validation message.
* v3 (`fellow68`), lavapipe, off vs on with `cpu/verify` (which now reads each entry from the frame's region, as the GPU
  does): City Lab walk (60 frames; 34 421 entries written by elements, 26 074 by the maps' catch-up, 93 maps, 559
  checks), Stress Lab temporal (120 frames, 5349 checks): **0 differing pixels, 0 mismatches**. With autotune switching
  persistent instance data on and off (the only tunable; evaluations every 20 frames in blocks of 8): City Lab walk (45
  of 93 frames off, 48 maps), Stress Lab temporal (64 frames off): **0 differing pixels, 0 mismatches**. With the
  Khronos validation layer, async compute, persistent instance data and autotune: Lights Lab walk, 0 differing pixels,
  no validation message.

## Result

In one process, off vs on (blocks of 30 frames, 480 block pairs in City Lab, 1600 in Bistro exterior):

| | City Lab | draw-heavy City Lab | Bistro exterior |
|---|---:|---:|---:|
| v1 (`m4_f68_*_pi_ab`): render thread | +1.3 % | +2.1 % | +9.2 % (opaque instance data 0.088 → 0.205 ms) |
| v1: GPU time | +0.9 % | +1.3 % | +0.6 % (pre-pass +0.019 ms) |
| v1: frame | +1.5 % | +1.3 % | +0.6 % |
| v2 (`m5_f69_*_pi_ab`): render thread | **−6.0 %** (−0.094 ms) | **−7.2 %** (−0.124 ms) | |
| v2: GPU time | +1.3 % (+0.024 ms: pre-pass +0.010) | +1.5 % (+0.028 ms: pre-pass +0.013) | |
| v2: frame | +0.2 % (n.s.) | **−2.4 %** | |
| v3 (`m5_f70_*_pi_ab`): render thread | **−13.5 %** (−0.184 ms) | **−4.1 %** (−0.066 ms) | −0.6 % (−0.008 ms) |
| v3: GPU time | +0.4 % (+0.008 ms, p 0.07) | +0.9 % (+0.019 ms: pre-pass +0.009) | +0.2 % (+0.010 ms: pre-pass +0.017) |
| v3: frame | +0.2 % (n.s.) | +0.4 % (n.s.) | +0.2 % |

v2's steps (City Lab): opaque instance data 0.178 → 0.081 ms, shadow lists' instance data 0.138 → 0.049 ms. What the
GPU pays is the slot's indirection in the vertex shaders (the depth pre-pass, the shadow passes) and, until v3, the
upload: one buffer update, one dispatch and the barriers before the first draws. v3 (no upload) costs the GPU a third
to two thirds of v2's time, and the render thread's saving in City Lab doubles (v2 spent part of it on the upload and in
the steps around it). In `m5_f70` the GPU ran slower than in `m5_f69` (City Lab 2.22 against 1.83 ms of GPU time per
frame, draw-heavy 2.06 against 1.89 ms): both City Lab variants were bound by the GPU there, so the frame does not show
the render thread's saving; whether a scene gains depends on which of the two limits it (hence the autotune tunable).

Candidate defaults (persistent instance data v2 + async compute + autotune) against `fellow67`'s defaults, between
processes (`m5_f69_*_cand`, 3 ABBA runs of 20 s): City Lab **−4.1 %** (3/3, p 0.015), draw-heavy City Lab **−4.6 %**
(3/3, p 0.024).

## Decision

(pending)
