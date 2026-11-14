# FellowEngine — milestone 112 report (cloth that tears and keeps off itself)

Status: **FINAL** (2026-11-14): the owner said "kontynuuj"; the laptop's job f432 done. Branch `fellow/geometry`. The readiness assessment's physics row listed "cloth
without self-collision or tearing": this milestone does both (decisions 0220 and 0221).

## 0. Result

`FellowCloth3D` tears: `tear(position, radius)` cuts a hole (a shot, a blade: a line of calls), and `tear_strain`
breaks the edges pulled past that fraction of their length — a sheet held at its corners that a ball goes through tears
open where it is pulled, a flag cut off its pole falls to the ground. `self_collision` keeps a cloth's layers apart: a
sheet falling in a heap lies in folds (the nearest two particles 8.5 cm apart) instead of through itself (0.65 cm, 56
particles within half the distance of another). Nothing changes for a cloth that uses neither: the earlier eleven
lanes of `cloth_check.gd` bit for bit the same, the regression scenes `cloth` and `cape` exact.

## 1. How a cloth tears

A broken edge's triangles go: degenerate in the mesh's index buffer (one upload; the vertices and the blend shape's
per-instance buffers stay as they are, so the motion vectors stay right), out of the wind's and the normals' lists.
Every edge left without a triangle goes with them (no invisible thread holds the pieces together), and every bending
pair across a gone triangle. The tethers — each free particle held within its path along the cloth to its nearest pin
— are found again along what is left: a part cut from every pin falls free, a part still hanging by a strip is held by
the path round the hole. A particle of no triangle left (a hole's inside) stays where it is, out of the bounds; a part
fallen 100 m below the cloth is dropped. `reset()` makes the cloth whole again; the signal `torn(position, edges)`
tells the game where.

## 2. Checks

`cloth_check.gd`'s five new lanes (cloud; the editor build and the release template the same; GodotPhysics3D and Jolt
the same):

| lane | result |
|---|---|
| `tear_cut`: `tear()` at a still curtain's middle, radius 0.25 m | 84 edges, 64 of 960 triangles; one signal, at the middle within 1 cm; the curtain hanging whole round the hole |
| `tear_reset`: the same, then `reset()` | whole again, hanging as before |
| `tear_strain`: a ball (0.3 m) through a sheet held at its corners at 8 m/s, `tear_strain` 0.5 | pushed 0.73 m back, then torn at the ball (24 edges): the ball through a star-shaped hole, the sheet on its corners |
| `tear_strain_off`: the same, 0 | nothing torn: the ball through between the particles, as before |
| `tear_off`: a flag in a breeze cut beside its pole (16 calls) | a clean cut (30 triangles); the cut part on the ground 2.8 s later, the strip on the pole hanging |

The laptop (job f432, the release template at 9ae3ede529): every lane passes with GodotPhysics3D and Jolt (the same
values with both); the lanes out of the wind the same values as the cloud's, those in the wind (the flags, the capes,
`tear_off`) a few per cent apart, as the gusts' and the turbulence's sines differ between MSVC and GCC. Its picture
strip: the hole, the star-shaped tear, the cut part downwind on the ground, the heaps.

The cost: nothing without tears; `tear_strain` one pass over the edges a frame — 64 flags in the wind on the laptop
(three alternating runs each, 20 s): the worker threads 25.3-26.5 ms a frame (summed over 24 threads) without, 25.6-27.4
with 0.5; the main thread 1.36-1.54 ms against 1.48-1.55.

## 3. Self-collision

Decision 0221: each frame the pairs of particles that may meet — within twice `self_collision_distance` now (0: three
quarters of the mean edge), not within twice it at rest (neighbours, which the cloth's constraints keep) — found by a spatial
hash (a particle's own cell and half of the 26 around it); in each substep a pair closer than the distance is pushed
apart along its line by the particles' inverse masses. `cloth_check.gd`'s `self_heap` / `self_heap_off`: a sheet let go
of its rail, falling in a heap — at 4 s the nearest two particles not neighbours at rest 8.47 cm apart (none within half
the distance) against 0.65 cm (56 particles) without. The cost: nothing when off; 64 flags in the wind with it, on the
laptop (three alternating runs each): the worker threads 25.9-29.3 → 31.7-37.4 ms a frame (+22 %, about 0.1 ms a flag
of 400 particles), the main thread (waiting for them) 1.47-1.56 → 1.69-1.94 ms.

## 4. The broadphase's update in the stats

For milestone 111's next step, FellowEngine's stats split GodotPhysics3D's broadphase update
(`physics_godot_bp_optimize_usec`, `_bp_cull_usec`, `_bp_pairs_usec`) and count the pairs made and freed
(`physics_godot_pairs_made`, `_freed`); Physics Lab reports them with the step's phases. The cloud's pile in its first
four seconds: at 5 000 bodies the culls 10.2 ms and the loop 3.4 ms a tick (1 300 pairs made a tick); at 10 000 the
culls 22.1 ms and the loop 20.2 ms (8 000 made and 7 200 freed a tick, as the taller pile falls through itself) — the
loop's time is the pairs'. The same hashes.

## 5. Next

Particles against triangles (thinner layers), friction between layers.
