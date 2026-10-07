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
* The first list element of a frame that uses a surface (any thread; an atomic exchange of the frame number on the
  surface, after a plain read so that later elements do not write the line) checks its entry. The instance carries a
  version (`fellow_data_version`, changed by every setter of a field the entry holds and by the frame's update of the
  previous transform); if the entry was built from the current version it is current. Otherwise the entry is built as
  stock builds the element's data and compared with the CPU copy; a changed entry is copied and queued. (A new mesh,
  material or other change that makes new surface caches gives them new entries.) Before a list's draws are recorded, the queued entries are uploaded (one buffer update and one
  scatter dispatch, 44 words per entry); the render graph orders the dispatch after the previous frame's reads.
* The scene shader (behind a specialization bit, so pipelines without it are unchanged) reads the element in the vertex
  and fragment stages (`fellow_element_init()`), then every other field from the entry (`FELLOW_INSTANCE()`); the flags
  and GI offset come from the element. Without the bit it reads the per-frame buffer as stock does.
* The buffer grows by doubling (the old content copied on the GPU). Slots are handed out when a surface cache is
  created and returned when it is freed.

## Why it is exact

For every element the shader reads the same values stock writes into that element: the entry was built by stock's code
for the same surface in the same frame (the fields it holds do not change between the lists of a frame: transforms are
set before rendering, the previous transform is updated by the frame's first list fill, before any instance data is
built), and the flags and GI offset are the element's own. `cpu/verify` rebuilds stock's data for every element and
compares it with the entry and the element (`_fellow_verify_elements`).

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

## Result

(pending)

## Decision

(pending)
