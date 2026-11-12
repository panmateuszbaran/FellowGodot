# 0182 — Destruction: Voronoi fracture on a worker thread, caps from the source's own cross-sections, rigid debris on a budget

Status: **KEEP** (a new node: nothing changes without it) · Branch `fellow/geometry` · Code:
`modules/fellow_world/fellow_fracture.{h,cpp}` (the fracture, pure arithmetic), `modules/fellow_world/fellow_destructible_3d.{h,cpp}`
(`FellowDestructible3D`), `modules/fellow_world/register_types.cpp` (the class, the monitor "FellowEngine/Debris
pieces"), `modules/fellow_editor/fellow_editor_plugin.{h,cpp}` ("Add Destructible Crate") · Checks:
`benchmarks/project/bench/destruction_check.gd` (thirteen lanes, `--many=N`), the regression scene `destruction`, the
editor check; the laptop (job f393).

## Problem

The readiness assessment's physics row said "no destruction": crates, pillars, walls and windows that break are in
every AAA action game, and Godot has nothing for them — a project fractures meshes by hand in a DCC tool and swaps in
the pieces itself.

## Change

**`FellowDestructible3D`** (a `Node3D`), a child of the body that is the object (a `StaticBody3D`, `RigidBody3D` or
`AnimatableBody3D`), breaks the body's `MeshInstance3D` (or `mesh_path`'s):

* **When**: `break_apart(position, impulse)`; `damage(amount, …)` taking `health` to zero; with `break_on_impact`, a
  blow of `break_impulse` N·s (40) or more. For a `RigidBody3D` the blow is its change of velocity in a physics step,
  less what gravity accounts for, times its mass — a crate dropped or hit, not one resting or under a load (the
  contacts' own impulses count the weight every step: a 600 kg crate at rest reports 98 N·s a step). For a static body
  it is a rigid body striking it: a shape query a step (0.3 m margin, a body up to 18 m/s seen the step before it
  arrives), its momentum towards the body.
* **The fracture** (`FellowFracture`, on a `WorkerThreadPool` task): `piece_count` sites inside the solid (a ray's
  crossings; a share `impact_focus` gathered around the impact, none nearer another than 2 % of the object's size; a
  thin object's on its middle plane so it is cut across, never split in layers), and for each site its Voronoi cell:
  the source's triangles clipped by the cell's planes (nearest first; each edge's cut computed from its ends in a fixed
  order, so both triangles of an edge cut it at the same point), and on each face of the cell **the source's own
  cross-section by that plane** — the loops its triangles' cuts make, chained, holes and islands by the even-odd rule —
  intersected with the cell's face (a convex polygon: a square cut by the cell's other planes) by Clipper2 and
  triangulated with its holes (polypartition's ear clipping). Each plane's cross-section is made once for the two cells
  it bounds. Parts of a cell that do not touch (both sides of a window) are pieces of their own.
* **The pieces**: a mesh around each piece's centroid (the source's surfaces, then the interior: `interior_material`,
  planar UVs × `interior_uv_scale`; normals, tangents, indexed — on the worker), a mass from its volume (divergence
  theorem) and `density`, and convex collision: its hull, or, when the hull is more than 2 % larger than the piece (a
  piece bent round a window's corner, an arc of a ring), the hulls of its parts, cut by the plane of one of its own
  source faces that has it on both sides (a reflex face), down to eight. Each piece a `RigidBody3D` (the body's layers
  and mask) with a `MeshInstance3D` and its `CollisionShape3D`s, placed where it was, with the body's velocity (and its
  spin's), thrown out from the impact at up to `burst_speed` (1.5 m/s, less farther away), and the `impulse` shared by
  the pieces near the impact (each moved alike: the momentum conserved). Velocities, not impulses: a new body's mass
  reaches GodotPhysics3D only at its next step (an impulse there moved a 199 kg piece as if it weighed 1 kg). The body
  is freed (`free_after_break`); signals `broken(pieces)`, `damaged(amount, health)`.
* **Prefracture**: with `prefracture` the pattern is computed when the node is ready, on the main thread, and shared
  (meshes and shapes) by every destructible of the same mesh, `seed` and `piece_count`: it breaks at once. Without it
  the break appears when the worker has finished (a few frames: below).
* **Budgets**: a frame spawns at most `get_max_pieces_per_frame()` pieces (128; a break over it waits a frame, intact;
  the first break of a frame always goes). The pieces are debris: asleep past `debris_lifetime` (10 s), or twice that
  old, a piece is frozen and shrinks away in 0.6 s (game time); past `get_max_debris()` (256) pieces in the game the
  oldest go first.

`get_stats()`, `get_global_stats()` (fracture, build and spawn times), the monitor "FellowEngine/Debris pieces";
configuration warnings when there is no mesh to break. The FellowEngine menu's "Add Destructible Crate" (a 40 kg
rigid crate breaking at 150 N·s: dropped from about 0.7 m).

## Checks (cloud: the CPU, lavapipe)

`bench/destruction_check.gd`, the editor build, 5.5 s at 60 fps, with GodotPhysics3D and with Jolt. For each lane's
pieces: their meshes' volumes summed against the source's, how open each is (the sum of its faces' area vectors over
its area: a closed surface's cancel), the lowest point of their hulls against the ground (at 3.3 s):

| lane | what is checked | GodotPhysics3D | Jolt |
|---|---|---|---|
| a 1 m crate told to break at a corner (12 pieces) | all pieces, volume, closed, resting | 12, 1.0000 m³ of 1, closed, 1.0 cm | 12, the same, 0.6 cm |
| a pillar (0.5 × 3 × 0.5 m) struck by a 20 kg ball at 10 m/s | breaks on the blow (16 pieces) | 195 N·s felt, 16, volume exact, 1.3 cm | 195 N·s, 16, 2.2 cm |
| the same struck by a 2 kg ball at 3 m/s | stays | 5.5 N·s felt, whole | 5.6 N·s, whole |
| a 40 kg crate dropped from 3.6 m (`break_impulse` 100) | breaks on landing, pieces not flung | 251 N·s, 12 pieces at ≤ 5.7 m/s | 277 N·s, ≤ 1.3 m/s |
| a 600 kg crate on the ground under a 900 kg one (`break_impulse` 40) | stays | 0 N·s felt | 0 N·s |
| a pane of glass (1.6 × 1.2 × 0.02 m) shot through its middle (30 pieces, focus 0.8) | cut across, volume | 30, 0.0384 m³ exact, 2.0 cm | the same |
| a wall with a window (CSG, baked; 20 pieces) | cuts with a hole, volume, no explosion | 20, 1.625 m³ exact, ≤ 2.1 m/s | the same |
| a ring (a torus, 14 pieces) | concave, curved: volume | 14, 0.6554 m³ exact, closed | the same |
| a crate damaged three times (health 50, 20 a blow) | breaks on the third | yes | yes |
| six crates prefractured (one mesh, one seed) | one pattern, at once | 1 pattern, pieces in the same call | the same |
| a crate whose debris lives 1 s | its pieces gone | 0 left at 5 s | 0 left |
| a sphere of 4 000 triangles in 32 pieces | volume, closed | 32, 2.1364 m³ exact | the same |
| the debris budget cut to 40 (220 pieces) | the oldest shrink away | 40 left | 40 left |

Every piece of every lane closed (openness below 10⁻⁵) and the volumes equal to the source's to 10⁻⁵ — the cut faces
are the source's own cross-sections, not caps chained from the clipped triangles' edges: the first version capped each
cut from the segments the clipping left, cut after cut, and lost 0.4 % (a crate) to 44 % (the sphere) of the volume to
loops that did not close; a cell of a concave mesh whose cap had a hole (a window) was filled across it. The deepest
hull point is the physics engine's resting penetration (Jolt's slop is 2 cm; a heavy shard of the sphere on its curved
face sinks 3.7 cm); none falls through.

Two fixes the lanes found: the wall's pieces were thrown at 41 m/s by the shared impulse (applied as an impulse before
GodotPhysics3D knew the new body's mass: velocities now), and Jolt rounded the shards' hulls by its default convex
radius (4 cm: a sharp tip sank that far; the pieces' shapes now have a 5 mm margin). A shard's flat hull ("could not
find a suitable initial triangle") is left out.

The fracture's time (cloud, editor build, on a worker): a 12-piece crate 1.5-2.5 ms, the 30-piece pane 9 ms, the wall
10 ms, the ring 48 ms (its pieces' convex parts), the 4 000-triangle sphere 51 ms; spawning a crate's 12 pieces
0.5-0.9 ms of the main thread. Faster by: each source triangle's bounding sphere skipping the cross-sections it misses,
cuts matched exactly instead of within a tolerance (searching 27 cells), the surface clipped between two buffers, a
convex source's cells needing no split into parts, the pieces' arrays built on the worker (the sphere: 69 → 38 ms of
fracture, its build 17 → 4 ms of the main thread); placing each piece before adding it to the tree instead of moving
it after (a crate's spawn 5.3 → 0.5 ms).

64 crates broken in one frame (cloud, two threads, GodotPhysics3D): prefractured, the budget spreads their 768 pieces
over 7 frames of 14-33 ms (the physics of the new piles included); computed on the workers, 2.1 ms of fracture each,
all broken within 42 frames.

Lavapipe: the lanes filmed (the crate's corner, the pillar knocked apart by the ball, the crate shattering on landing,
the pane's shards, the wall breaking round its window, the ring's pieces with their cut faces). The regression scene
`destruction` (a crate, a pillar and a pane prefractured and broken as it starts): the same picture on every run.

## Results on the laptop (job f393, release template at 0f02e28)

The thirteen lanes with GodotPhysics3D and with Jolt: all as on the cloud (the same pieces, volumes exact, every piece
closed; the deepest hull point 4.7 cm on GodotPhysics3D, a heavy shard of the sphere 7.1 cm on Jolt; the debris cut to
40). The fractures on the 24 threads, five breaks in one frame (their times overlapping): the crate 3.0-3.1 ms, the pane
8.0 ms, the wall 8-10 ms, the ring 51-74 ms, the sphere 55-71 ms; spawning a crate's 12 pieces 0.2-0.4 ms of the main
thread, the sphere's 32 0.8 ms (GodotPhysics3D).

Crates broken at once, prefractured (headless: the CPU's whole frame; Jolt twice):

| | 64 crates (768 pieces) | 256 crates (3 072 pieces) |
|---|---|---|
| frames to break them all (128 pieces a frame) | 7 | 26 |
| the frames meanwhile, Jolt | 0.9-3.7 ms | 2.1-3.9 ms |
| the frames meanwhile, GodotPhysics3D | 2.6-7.3 ms | 3.3-7.6 ms |
| a crate's spawn (main thread) | 0.11-0.22 ms | 0.11-0.17 ms |
| the frames after, 256 pieces of debris (mean / largest), Jolt | 0.34 / 1.4-2.1 ms | 0.63 / 2.8-3.0 ms |

Computed on the workers the headless frames were far shorter than the fractures, and the check ended (240 frames) before
any crate had broken: the bench now waits until every crate has broken, and builds a piece's meshes and shapes only in
the frame it may spawn (the budget covers them too); measured again in job f394.

## Decision

Keep. On the CPU, at the moment of the break (or once, prefractured): the pieces follow the blow (smaller where it
struck), and need no authored pieces; the worker threads keep the frame free of the fracture.

## Rejected / not done

* **Capping each cut from the clipped triangles' edges** (chaining the segments the clipping leaves): open pieces and
  lost volume after a few cuts (above); the source's own cross-sections, clipped to the cell's face, are exact.
* **One hull per piece always**: a piece of a concave source wraps round a corner (a window's), and its hull reaches
  into its neighbours where they start: the physics engine pushes them apart.
* **Applying the impulse with `apply_impulse()`**: wrong on GodotPhysics3D in the step a body is made (above).
* **The contacts' impulses as the blow on a rigid body**: they include its weight every step.
* **Fracture on the GPU, authored chunk hierarchies, connectivity graphs (a wall's pieces holding until their support
  goes), damage that cracks without breaking**: next; a broken wall's pieces all fall now.
