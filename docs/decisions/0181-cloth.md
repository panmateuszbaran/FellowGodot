# 0181 — Cloth: position-based dynamics on the CPU in small substeps, the scene's wind, motion vectors through a blend shape

Status: **KEEP** (a new node: nothing changes without it) · Branch `fellow/geometry` · Code:
`modules/fellow_fluid/fellow_cloth_3d.{h,cpp}` (`FellowCloth3D`), `modules/fellow_fluid/register_types.cpp` (the
monitor), `modules/fellow_editor/fellow_editor_plugin.{h,cpp}` ("Add Flag", "Add Curtain") · Checks:
`benchmarks/project/bench/cloth_check.gd` (ten lanes, `--many=N`), the regression scene `cloth`, the editor check; the
laptop (job f392).

## Problem

The readiness assessment's characters and physics rows both said "no cloth": a flag, a banner, a curtain, an awning, a
sail or a cape is in every AAA scene, and FellowEngine's weather (wind with gusts) had nothing to blow but plants and
particles. Godot's `SoftBody3D` is a general volumetric soft body: no wind, no aerodynamics, its own physics-step
coupling, and its vertex updates carry no motion vectors (TAA and FSR smear a moving soft body).

## Change

**`FellowCloth3D`** (a `GeometryInstance3D`): a rectangle (`size`, `resolution`: from the node's origin along +X and
down, facing +Z) or a `source_mesh`'s first surface. Its vertices welded by position are the particles (a mesh's seams
of UVs or normals stay one particle), with mass from the triangles' areas (`mass` per square meter); each edge is a
stretch constraint, the two vertices across each pair of triangles a bending constraint, each free particle tethered to
its nearest pin along the cloth (Dijkstra over the edges at build time; no farther than at rest times `tether_slack`).
`pin_mode`: the top edge, the left edge, the top corners, the four corners, or the source mesh's vertices whose colour's
red is 0.5 or more; `set_particle_pinned()` pins or lets go at run time.

The solver (world space): fixed substeps (`substep_rate` 480 Hz: 8 a frame at 60 fps; at most `max_substeps`, a slow
frame simulating less than its time), each one integrating gravity and the air, `iterations` (2) Gauss-Seidel passes
over the stretch constraints (alternating their order), one over the bending pairs, the tethers, the collisions, then
velocities from the positions. Pinned particles follow the node, interpolated between the frames' transforms.

The air: the active `FellowWind3D`'s velocity at the cloth's centre (its gusts) times `wind_scale`, plus `wind`, varied
over the cloth and time by `turbulence` (a field of sines, per triangle once a frame); on each triangle the air against
the triangle's own velocity pushes along its normal (`drag`, ½ρ·area·(v·n)|v|) and across it (`lift`) — a flag streams
and flaps by itself, a curtain sways.

Collisions: the particles stay `thickness` off spheres, capsules, boxes, cylinders, planes and height maps, with
`friction`; the shapes in `colliders` (a `CollisionShape3D`, or every shape of a `CollisionObject3D`, read each frame:
a character's body moves with it) and, with `collide_with_world`, the physics world's bodies overlapping the cloth (one
`intersect_shape` a frame on `collision_mask`). Convex and concave shapes are not collided with.

**Threads**: every cloth of a frame is prepared on the main thread (transforms, the wind, the colliders), simulated at
once on the `WorkerThreadPool` (each cloth one task), then uploaded — after the game's processing (the node's
`process_priority` is 100).

**Motion vectors**: the mesh has one blend shape (no offset). The RenderingServer then gives each instance vertex
buffers of its own, written by its skinning kernel when the instance changes and swapped each frame it does; the cloth
writes its base vertices each frame and sets the blend shape's weight, so that the previous frame's vertices are in the
other buffer and the motion vectors (TAA, FSR) follow the cloth. The normals are the particles' (from the triangles),
each vertex's own normal and tangent at rest turned as the cloth turned there.

**Level of detail**: past `lod_distance` (25 m) from the camera half the substeps; past `simulation_distance` (100 m)
the cloth is carried with the node without simulating. In the editor it simulates while the Animate toggle is on.

`get_stats()`, `get_global_stats()` (the worker threads' and the main thread's time), the debugger's monitor
"FellowEngine/Cloth time", `apply_impulse()`, `reset()` (also when the node moves more than 5 m in a frame).

## Checks (cloud: the CPU, lavapipe)

`bench/cloth_check.gd`, four seconds at 60 fps, the wind 7 m/s with gusts (GodotPhysics3D):

| lane | what is checked | result |
|---|---|---|
| a flag on a pole (left edge pinned, 400 particles) | streams downwind | free edge 1.76 m out (of 1.8), mean strain 0.43 % |
| a curtain (top edge pinned, no wind) | hangs straight, its length kept | bottom at 0.60 m (2.40 m below the rail), strain at most 0.7 % |
| a sheet dropped on a sphere and a box (the world's colliders) | rests on them, nothing inside | deepest 0 cm, resting between 0.02 and 1.05 m |
| a sheet dropped on a height map | rests on it | deepest 0 cm |
| a cape pinned at two corners on a node circling a capsule at 1.6 m/s | stays out of the capsule | deepest 0 cm, mean strain 2.3 % (whipping in the wind) |
| an awning pinned at its corners | sags | 0.24 m in the middle, mean strain 0.5 % |
| a banner from a `PlaneMesh` (source mesh) | hangs from its top edge | top at 3.00 m, bottom at 1.00 m |
| a flag hit by an impulse (no wind) | settles | mean strain 0.33 % at the end |
| a flag past `lod_distance` | still streams | free edge 1.60 m out, mean strain 0.95 % |
| a flag past `simulation_distance` | not simulated | carried |

Every particle finite in every lane. The largest strain is at a corner holding the cloth alone (an awning's corner
11.7 %, a hanging flag's top corner 13 %): stretch concentrates there in real fabric too, and the mean stays under 1 %.

Stretch against substeps and passes (a flag hanging from its left edge, an awning from its corners, 2.5 s):

| substep rate, passes | flag: mean / largest strain | awning: mean / largest | time a frame (cloud) |
|---|---|---|---|
| 480 Hz, 1 | 0.64 % / 8.0 % | 0.82 % / 8.8 % | 0.28 ms |
| **480 Hz, 2 (default)** | 0.17 % / 2.7 % | 0.70 % / 14 % | 0.35 ms |
| 480 Hz, 4 | 0.12 % / 1.9 % | 0.34 % / 7.9 % | 0.54 ms |
| 960 Hz, 1 | 0.06 % / 1.0 % | 0.35 % / 7.2 % | 0.51 ms |
| 1440 Hz, 2 | 0.03 % / 0.5 % | 0.11 % / 2.6 % | 1.01 ms |

The cost of a 400-particle flag (cloud CPU, editor build): 0.39 ms a frame of a worker thread, of which the air on the
triangles 0.09 ms, the bending pass 0.05 ms, each stretch pass 0.10 ms; reading the solver's arrays through raw pointers
instead of the checked vectors saved 10 %. Sixteen flags on the cloud's two threads: 6.4 ms of the workers, 3.3 ms of
the main thread (waiting); all past `lod_distance`: 3.6 and 1.9 ms.

Lavapipe: the lanes filmed (the flag streaming and flapping, the sheet draped over the sphere and the box, the cape on
the capsule, the awning's sag); the flag drawn with Godot's motion-vector debug view: vectors on the cloth only. The
regression scene `cloth` (a flag in gusts, a curtain, a sheet on a sphere and a box): the same picture on every run.

## Results on the laptop (job f392, release template at 98630ca)

The ten lanes as on the cloud (the same decisions and strains). Flags in the wind (`--many=N`, each 400 particles,
headless: the CPU's whole frame; 15 s, two runs each; the laptop's 24 threads):

| | 64 flags | 64, past `lod_distance` | 256 flags | 256, past `lod_distance` |
|---|---|---|---|---|
| the main thread a frame (preparing, waiting, uploading) | 1.61 / 1.40 ms | 0.85 / 0.85 ms | 5.56 / 5.95 ms | 3.21 / 3.63 ms |
| the worker threads a frame (summed) | 25.0 / 26.1 ms | 14.9 / 15.1 ms | 117 / 126 ms | 65 / 74 ms |
| the frame | 1.63 / 1.42 ms | 0.87 / 0.88 ms | 5.63 / 6.04 ms | 3.26 / 3.69 ms |

A flag costs 0.40 ms of a thread a frame (8 substeps), about the cloud's: 64 flags simulated in full take 1.4-1.6 ms
of the frame, 256 (100 000 particles) 5.6-6.0 ms. The six lanes filmed on the RTX (Forward+, `--write-movie`): the flag
flapping in the gusts, the sheet settling over the sphere and the box, the cape whipping round the capsule.

After the measurement each constraint's share of the correction is kept with it (set with the pins: 0.39 → 0.38 ms a
flag on the cloud); a reciprocal square root by Newton's steps instead of the hardware's was slower (0.43 ms) and
dropped.

## Decision

Keep. The CPU and small substeps rather than the GPU: a scene's cloths are hundreds to thousands of particles, which
the worker threads simulate in a fraction of a millisecond each, on every renderer (Compatibility included), with the
physics world's shapes and the gameplay's queries at hand; the blend shape gives the motion vectors that a vertex
buffer updated from the CPU would not have.

## Rejected / not done

* **A GPU solver** (compute, the positions in a texture read by the material's vertex shader): a material of its own
  instead of the project's, the physics world's shapes copied to the GPU each frame, and motion vectors only through
  Godot's previous-time vertex pass guessing which texture is the previous frame's.
* **Godot's `SoftBody3D`**: no wind, no motion vectors, its own coupling to the physics step.
* **One pass with twice the substeps**: less stretch for the same time on a hanging flag, more on an awning; the
  default keeps 480 Hz and two passes.
* **Self-collision, tearing, a character's skinned cloth** (pins following a skeleton's bones, the cloth bound to a
  skinned mesh): next; a cape now hangs from a node (a `BoneAttachment3D`'s, one frame behind its bone).
