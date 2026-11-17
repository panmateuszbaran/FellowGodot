# 0259 — Streamed meshes' swaps cross over

Status: **KEEP** (with `FellowWorldStreamer3D.crossfade`, decision 0258; 0 s by default: a switch) · Branch
`fellow/geometry` · Code: `modules/fellow_world/fellow_world_streamer_3d.{h,cpp}` (the swaps' fades) · Checks:
`benchmarks/project/bench/dithered_fade_check.gd` (section 7), the editor check.

## Problem

A cell's large meshes are streamed by distance (decision 0174): a stand-in of 15 % of the triangles in the cell's
scene, the full mesh loaded within `mesh_distance` and swapped in, dropped again beyond it. The swap is a switch — near
the camera, where the difference between the two shows most. Decision 0258 crossed the cells over with their proxies;
the meshes inside them still popped.

## Change

With `crossfade`, a swap — the full mesh in, or the stand-in back — keeps the outgoing mesh on screen for the same
time, drawn by a RenderingServer instance of its own (the node's transform, material override and surface overrides,
layers; no shadow: the node casts its new mesh's), keeping the other share of the pixels while the node's new mesh's
grows (decision 0258's `fellow_instance_set_dither_alpha()`); then the instance is freed and the outgoing mesh released
(kept alive until then: when the full mesh goes, the fade holds its last reference). A swap back while crossing drops
the earlier instance; a mesh in a cell that is itself crossing over swaps at once (the content's share is the cell's).
`is_settled()` waits for the swaps' fades.

## Verification

`dithered_fade_check.gd` section 7 (lavapipe, 960 × 540, without TAA): a cell of one ball (a sphere of 128 × 64 faces,
4 m across, streamed: its stand-in 15 % of the triangles), loaded, the camera 40 m away, `crossfade` 0.5 s:
`mesh_distance` moved past the camera (the full mesh loads and crosses over: 30 frames) and back (the stand-in crosses
over: 30 frames); halfway in either direction no hole where both meshes would cover; settled back, the picture the
stand-in's, exactly. The editor check (a level streamed back) as before.

## Results

On the RTX (job f474, three runs): section 7 as on lavapipe (no hole, 30 frames each way). Frontier Lab with its
streamed meshes swapping within 160 m and crossing over: decision 0258's table (job f474), the same frame time as
switching.

## Decision

Keep, with `crossfade`: the last pop of a streamed cell — its large meshes sharpening as they come near — fades too.

## Rejected / not done

* **Both meshes on the node** (a second surface set): a node draws one mesh; an instance of its own needs no change to
  `MeshInstance3D`.
* **The outgoing mesh's shadow**: two shadows of nearly the same mesh for half a second; the node's new one alone.
