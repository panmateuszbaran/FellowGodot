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
* Every list element checks its surface's entry (any thread). The instance carries a version (`fellow_data_version`,
  changed by every setter of a field the entry holds and by the frame's update of the previous transform); if the entry
  was built from the current version it is current, and the check only reads (the surface's cache line stays shared
  between the threads of the different lists). Otherwise the first element of the frame that meets it (an atomic
  exchange of the frame number on the surface) builds the entry as stock builds the element's data and compares it
  with the CPU copy; a changed entry is copied and queued. (A new mesh, material or other change that makes new
  surface caches gives them new entries.) The queued entries are uploaded when the next render pass uniform set is set
  up, i.e. before any draw that reads them is recorded, all lists filled since the last upload in one dispatch.
* First RTX measurement (`fellow66`, `m4_f68_*_pi_ab`, in one process): with an exchange of the frame number for every
  surface on its first element of the frame (a write of every surface's cache line, which the shadow lists' threads
  had just read), the opaque list's step got slower (0.18 → 0.25-0.27 ms) while the shadow lists' halved (0.14 → 0.07
  ms): City Lab frame +1.5 %, draw-heavy +1.3 %; and one upload per list (two dispatches per frame, +0.017 ms GPU). Hence
  the read-only check and the single upload above. Before a list's draws are recorded, the queued entries are uploaded (one buffer update and one
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
