# FellowEngine — milestone 80 report (destruction that holds)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0188-structural-destruction.md`.

## 0. Result

* **`FellowDestructible3D.structural`**: a wall, a pillar, a façade broken a piece at a time where it is struck — a
  ball punches a hole and the rest stands, a pillar cut at its foot falls in one piece, a wall cut across drops its
  upper part — the support found through the pieces that touch down to the ground; its own debris does not strike it.
* **Checked**: four lanes with GodotPhysics3D and Jolt, decision 0182's thirteen unchanged, filmed, a regression scene
  (`structure`), the editor ("Add Destructible Wall").

## 1. Development

* `modules/fellow_world/fellow_destructible_3d.{h,cpp}`: the pieces' contacts when a fracture is prepared, the standing
  structure, strikes, support and falling parts, collision exceptions while pieces leave; `doc_classes/FellowDestructible3D.xml`.
* `modules/fellow_editor/fellow_editor_plugin.{h,cpp}`: "Add Destructible Wall".
* `benchmarks/project/bench/structure_check.gd`; `tools/fellow_regression`: the scene `structure`.

## 2. Verification

* The lanes on the editor build (cloud) with GodotPhysics3D and Jolt, and on the release template: as expected
  (decision 0188's table); `destruction_check.gd`'s thirteen lanes on both engines as before.
* Lavapipe: the lanes filmed; the regression scenes `structure`, `destruction` and `vehicle` exact, the release
  template's the same.
* The editor check passes (one start crashed before opening the project; the rerun passed).
* The laptop: not run (not linked to the session).

## 3. Measurements

A strike on a standing 40-piece wall: its pieces' bodies made on the main thread as decision 0182's (0.2-0.4 ms a
crate's 12); the contacts between pieces found once, when the fracture is prepared.

## 4. Next

Stress and loads (beams that sag and crack), pieces held by other objects (a wall on a frame), rigid objects that lose
parts.
