# FellowEngine — milestone 148 report (free climbing; sound round edges)

Status: **FINAL** (2026-11-18). Branch `fellow/geometry`. The CPU and lavapipe in the cloud only: the GPU stays paused
by the owner (the jobs prepared for it, `f510` and `f511`, wait in `.tools/wip`).

## 0. Result

* **Free climbing** (decision 0276): `FellowTraversal3D.free_climb` — a body pushing at a climbable face (a layer of
  `climb_mask`) with nothing to mantle onto, or caught against it in the air, climbs it: up and down, along it (the
  wish held turning with the face), round a round tower and round outside corners, onto inside ones, over the top (a
  mantle, or a hang with no room there), down to the floor; a lunge over the face with `hang_jump()`
  (`climb_jump_distance`, as far as the face goes). The top is found by the climb itself (the edge where the face's
  plane meets the top's): a slab's face leaning back, which the ledge check took for a slope, is climbed and topped.
  The mantle's path now keeps the capsule off a slab's face and on a sloping top's plane (it went into a top sloping
  away). `climb_check.gd`'s ten new lanes pass on GodotPhysics3D and Jolt with no frame of the capsule in the world;
  a climbing body costs 18 µs a frame (33 µs before the traversal stopped rebuilding its query shape every call).
* **Sound round edges** (decision 0277): `FellowAcoustics3D.edge_diffraction` (opt-in; "Add Acoustics" turns it on)
  — a blocked source with no opening is heard round the edge of what blocks it, its side or its top, when that is
  louder: a source behind a pillar 2 m wide −21.3 dB with 46 % of its energy above 2 kHz and +1.7 dB to the right
  (the edge's side) instead of −40 dB and 2 % through it (stock: −12.8 dB, 62 %). The path found is kept while it
  holds: 64 sources +45 µs a frame; with 256 among walls the ray budget tests 47 a frame instead of 102.
* **Unchanged**: `climb_check.gd`'s fifteen earlier lanes and every lane of `traversal_check.gd` the same as the
  build before; the regression scenes `traversal` and `climb` exact; every earlier lane of `acoustics_check.gd` the
  same (the doorway's −25.8 dB, 30 %, +2.86 dB as in milestone 99).

## 1. Checks (cloud)

* `climb_check.gd` (25 lanes) and `traversal_check.gd` on GodotPhysics3D and Jolt; the earlier lanes compared value
  for value with a build of the previous commit; the regression scenes `traversal` and `climb`.
* `acoustics_check.gd` (headless on both engines, and under Movie Maker on lavapipe with `--listen=1`); `--many=64`
  and `--many=256` with and without `--edges=1`.
* The editor check: "Add Acoustics" with edge diffraction on, without a warning, undone.
* The class reference: `FellowTraversal3D.xml` and `FellowAcoustics3D.xml` from the doctool, their descriptions
  written, `make_rst.py` without a warning.

## 2. Next

On the GPU when the owner allows it: `f510` (the hang, 40 runs with the richer checkpoints) and `f511` (decision 0272's
A/B on Frontier Lab and Slice Lab). Meanwhile on the CPU: more of the readiness assessment's gaps.
