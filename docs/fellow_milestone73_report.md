# FellowEngine — milestone 73 report (cloth)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record: `docs/decisions/0181-cloth.md`.

## 0. Result

* **`FellowCloth3D`**: flags, banners, curtains, awnings and capes — a generated rectangle or a source mesh, pinned by
  an edge, corners or vertex colours — simulated on the CPU by position-based dynamics in fixed substeps (480 Hz, two
  stretch passes, bending, tethers to the pins), blown by the scene's `FellowWind3D` (drag and lift on each triangle,
  turbulence), kept out of spheres, capsules, boxes, cylinders, planes and height maps (shapes it is given and followed
  as they move, the physics world's under it), every cloth of a frame on the worker threads at once.
* **Motion vectors**: the cloth's mesh has one blend shape, so the RenderingServer gives each instance vertex buffers
  of its own, swapped every frame; TAA and FSR see the cloth move (Godot's motion-vector view: vectors on the cloth).
* **Checked**: ten lanes (a flag streams 1.77 m out in 7 m/s gusts, a curtain hangs 2.40 m below its rail, sheets rest
  on a sphere, a box and a height map with nothing inside them, a cape stays out of the capsule it is pinned on, an
  awning sags, a banner from a `PlaneMesh`, an impulse, the two levels of detail); a regression scene; the editor.
* **Cost** (the laptop, release template): a 400-particle flag 0.4 ms of a thread a frame; 64 flags in the wind
  1.4-1.6 ms of the frame on its 24 threads, 256 flags (100 000 particles) 5.6-6.0 ms; past `lod_distance` about 45 %
  less.

## 1. Development

* `modules/fellow_fluid/fellow_cloth_3d.{h,cpp}`: the build (welding, constraints, tethers by shortest paths, the mesh
  with its blend shape), the solver, the air, the colliders (given and queried), the group on the worker threads, the
  vertices written with each vertex's normal and tangent turned as the cloth turned, levels of detail, the API.
* `modules/fellow_fluid/register_types.cpp`, `config.py`: the class; the monitor "FellowEngine/Cloth time".
  `doc_classes/FellowCloth3D.xml`; an icon.
* `modules/fellow_editor/fellow_editor_plugin.{h,cpp}`: "Add Flag (cloth on a pole)", "Add Curtain (cloth)".
* `benchmarks/project/bench/cloth_check.gd` (the lanes, pictures, the motion vectors drawn, a grid for a movie,
  `--many=N`); `tools/fellow_regression`: the scene `cloth`; the editor check: the icon, the setups, the monitor.

## 2. Verification

* The lanes on the editor build (cloud): all as expected; stretch against substeps and passes measured on a hanging
  flag and an awning (decision 0181).
* Lavapipe: the lanes filmed; the flag in the motion-vector view; the regression scene `cloth` exact on two more runs
  (blessed again after the solver kept each constraint's share of the correction: the arithmetic's order changed).
* The editor check: the icon, "Add Flag" and "Add Curtain" without a warning and undone, the monitor.
* The laptop (f392, release template): the lanes; 64 and 256 flags at full quality and past `lod_distance`, twice
  each; the lanes filmed on the RTX.

## 3. Measurements

| the laptop, flags in the wind | 64 | 64, LOD | 256 | 256, LOD |
|---|---|---|---|---|
| the frame | 1.42-1.63 ms | 0.87 ms | 5.63-6.04 ms | 3.26-3.69 ms |
| the worker threads (summed) | 25-26 ms | 15 ms | 117-126 ms | 65-74 ms |

The cost of a flag (cloud): the air 0.09 ms, the bending pass 0.05 ms, each stretch pass 0.10 ms of 0.38 ms.

## 4. Next

A character's cloth (pinned to a skeleton's bones, bound to a skinned mesh), self-collision, tearing; the solver in
SIMD over coloured constraints (the scalar Gauss-Seidel is about 11 ns a constraint).
