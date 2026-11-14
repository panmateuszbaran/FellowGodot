# FellowEngine — milestone 116 report (cloth following the skin; large uploads off the frame)

Status: **FINAL** (2026-11-14). Branch `fellow/geometry`. Decision 0184's cloth pins follow their nearest bones,
rigidly; a garment's attached edge sits where the body's skin blends bones. And milestone 115's decision 0227 (large
uploads submitted on their own thread) measured on the RTX.

## 0. Result

* **Cloth following a skinned mesh's skin** (decision 0230): a garment's pinned edge on its skinned place, blended between
  bones as Godot skins the mesh (a skirt's waistband 51.5 mm → 0 from the body's skin, 0.02 mm from Godot's own
  skinning), the free part within a distance of its own — the readiness assessment's "not a skinned garment's vertices".
* **No frame waits for a large copy** (decision 0227, now on): Sponza streamed made the frame wait for copies of more than
  4 MiB 22-24 times a flythrough (54-60 with a 256 MiB budget); with large uploads submitted on their own thread, none.
  The GPU's frame the same; decision 0225's short trim no longer stalls with it. The CPU's rarer stalls (about one in
  four flythroughs) are elsewhere: counted frame by frame, they are not the fences', the transfers', the pipelines' or
  the presentation's.
* The build without a warning (`FellowTerrain3D`'s locals shadowing `Node::data`).

## 1. Cloth following a skinned mesh's skin (decision 0230)

`FellowCloth3D.follow_skin` (off), `skin_max_distance`, `skin`: a `source_mesh` with bone weights — a skinned mesh's
surface, in the skeleton's space — gives each particle its skin (its first vertex's four heaviest binds and weights);
each frame every particle's skinned place is computed as Godot skins the mesh (linear blend skinning; the binds from a
`Skin`, or the skeleton's rest without one). The pinned particles follow their skinned places (interpolated over the
substeps), the free ones are kept within `skin_max_distance` of theirs, the cloth starts on the body, and past
`simulation_distance` the whole cloth is skinned.

`cloth_check.gd`'s skirt (200 particles, its waistband weighted half to the hips and half to the spine and pinned) on a
skeleton circling 2.5 m, its hips swaying 20° and its spine twisting 25° against them, a capsule on its hips, in the
flags' gale; the same mesh on a hidden `MeshInstance3D`, skinned by Godot:

| | the waistband from its skinned place | from Godot's own skinning of the mesh | the farthest free particle from its skinned place |
|---|---:|---:|---:|
| following the skin (12 cm) | 0.0 mm | 0.02 mm | 12.0 cm |
| pinned to the nearest bones (decision 0184) | 51.5 mm | 51.49 mm | 17.98 cm |

Nothing into the hips' capsule either way. The earlier lanes the same, the regression scenes `cloth` and `cape` exact,
the editor build and the release template the same.

## 2. Large uploads off the frame (decision 0227 on the RTX)

Milestone 115's option (`rendering/fellow/frame/transfer_large_own_submit`): an upload above 4 MiB made off the render
thread submits and waits for its worker's earlier work and its own copy itself. FellowEngine's statistics now count the
frame's own copies over 4 MiB — those submitted at the frame's end with the semaphores the frame waits on
(`transfer_frame_large_submits`): on lavapipe's Streaming Lab 238 a flythrough without the option, 1 with it.

The RTX, Sponza streamed at 1920 × 1080 (job f444: eight alternating runs each; a stall: a frame over 16.7 ms whose GPU
work is under 60 % of it):

| | the frame's copies over 4 MiB, a flythrough | stalls in 8 runs | p99.9 | GPU frame |
|---|---:|---:|---:|---:|
| the defaults | 22-24 | 2 | 10.8-12.2 ms | 6.24 ms |
| large uploads on their own thread | 0 | 3 | 10.8-12.4 ms | 6.24 ms |
| and the staging trimmed after 0.5 s | 0 | 2 | 10.9-11.5 ms | 6.27 ms |
| a 256 MiB budget: the defaults | 54-60 | 3 | 11.0-12.4 ms | 6.25 ms |
| the budget, large uploads on their own thread | 0-1 | 1 | 11.1-12.8 ms | 6.24 ms |
| the budget, and the trim after 0.5 s | 0 | 2 | 11.0-12.8 ms | 6.25 ms |

Job f442's five runs a variant had suggested more (no stall in five runs with both, against four of five with the
defaults); eight runs a variant (job f444) put the stalls at about one in four runs in every variant. What stays: the
frame no longer waits for large copies, and the short trim — which stalled in every run without the option (22 stalls in
five runs) — no longer does (2 in eight). The option is on by default; decision 0225's idle time stays 5 s.

Job f445 recorded FellowEngine's counters frame by frame (three runs each, the defaults and both options): the
defaults' three stalls (25-30 ms frames, the GPU's 5-6 ms) had no fence wait (4 µs against 3.5-5 ms in the frames
around them), no transfer wait, no pipeline or presentation wait — CPU time elsewhere; one came with a 26 ms texture
creation on a streaming thread. Both options: no stall in three runs. Bistro exterior (no streaming, job f443): the same
frames.

## 3. The build without warnings

The editor build and the release template built with six `-Wshadow` warnings, all `FellowTerrain3D`'s locals named
`data` (shadowing `Node::data`); renamed, the terrain regression scenes the same as their goldens.

## 4. Next

The CPU's remaining stalls in Sponza streamed (one in four flythroughs: a sampling profiler on the laptop); a garment's
painted distance (a distance a vertex); cloth edges against edges.
