# FellowEngine — milestone 75 report (hanging and ladders)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0183-hanging-and-ladders.md`.

## 0. Result

* **Hanging**: `FellowTraversal3D` catches, in the air, a ledge the body cannot climb (no room on its top) — or with
  `hang_first` any it could — and hangs from it; the body moves along the edge as the controller asks (following it
  round walls and over small steps, stopping where it ends, at gaps and obstacles), climbs when pushed at the wall
  again (a mantle from the hang) and lets go when pulled away.
* **Ladders**: `FellowLadder3D`, a marker with rails and rungs drawn in the editor; the body takes one it walks at
  (or walks off its top over it), climbs it as asked, steps off at its foot and mantles off at its top.
* **The pose**: hands on the edge stepping along it, feet on the wall; hands and feet on the rungs, a rung apart.
* **Checked**: six lanes with GodotPhysics3D and Jolt (no frame with the capsule in the world), milestone 72's course
  unchanged, a regression scene, the editor ("Add Ladder", the ladder's gizmo).

Also in this milestone (destruction, decision 0182): a broken object's meshes and shapes are built in the frame its
pieces may spawn (within the budget of pieces a frame); `destruction_check.gd --many=N` waits until every crate has
broken.

## 1. Development

* `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`: the hang (catching, the edge found again along it, the climb
  and the release), ladders (finding one, climbing, the foot and the top), the pose's targets for both, the API and
  signals; `fellow_ladder_3d.{h,cpp}`; `register_types.cpp`, `config.py`, `doc_classes/FellowLadder3D.xml`,
  `FellowTraversal3D.xml`, an icon.
* `modules/fellow_editor/fellow_editor_plugin.{h,cpp}`: the ladder's gizmo, "Add Ladder (3 m, climbable)".
* `benchmarks/project/bench/climb_check.gd` (the lanes, pictures); `tools/fellow_regression`: the scene `climb`; the
  editor check: the icon, the gizmo, the setup.

## 2. Verification

* The lanes on the editor build (cloud) with GodotPhysics3D and Jolt: all as expected (decision 0183's table);
  `traversal_check.gd`: the nineteen lanes as before.
* Lavapipe: the lanes filmed; the regression scene `climb` exact on a second run; the whole suite with the release
  template.
* The editor check: the ladder's icon and gizmo, "Add Ladder" without a warning and undone.
* The laptop (f394, release template): the six lanes with both engines as on the cloud; filmed on the RTX (Jolt).

## 3. Measurements

A traversal's step while hanging or on a ladder (its probes along the edge included): 1.6-1.8 µs on average on the cloud;
on the laptop 1.9 µs with Jolt, 2.4 µs with GodotPhysics3D.

## 4. Next

Corners (wrapping round a ledge's outer and inner corners), jumps between ledges, clips for hanging and ladders.
