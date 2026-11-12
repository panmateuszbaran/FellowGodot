# FellowEngine — milestone 81 report (round corners and across gaps)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0189-ledge-corners-and-jumps.md`.

## 0. Result

* **Corners**: a hanging body (`FellowTraversal3D`, decision 0183) no longer stops where its ledge ends at a corner: it
  goes round an outside corner onto the wall's side, or round an inside one onto a wall meeting the ledge
  (`wrap_corners`, on), when both hands fit on the new edge and the body has room; the stick held as it was goes on
  along the new edge.
* **Jumps**: `hang_jump()` across a gap to the next ledge along the edge, or up to a ledge over it; the hands let go
  and catch the new edge.
* **Checked**: four new lanes with GodotPhysics3D and Jolt, decision 0183's six and decision 0180's course unchanged,
  filmed, a regression scene (`ledge_corners`), the release template.

## 1. Development

* `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`: the corner search (inside and outside), the jump search
  (along and up), the transfer (a curve for the feet, the facing turning, the hands moving and letting go), the wish
  held round a corner, the look repeated 0.2 s after one in vain; `wrap_corners`, `hang_jump_distance`,
  `hang_jump_height`, `hang_jump()`, `is_transferring()`, `hang_transferred`, `TRANSFER_*`;
  `doc_classes/FellowTraversal3D.xml`.
* `benchmarks/project/bench/climb_check.gd`: four lanes; `tools/fellow_regression`: the scene `ledge_corners`.

## 2. Verification

* The lanes on the editor build (cloud) and the release template, GodotPhysics3D and Jolt: as expected (decision
  0189's table), no frame of the capsule in the world; decision 0183's six lanes the same numbers as before;
  `traversal_check.gd`'s course passes.
* Lavapipe: the lanes filmed; the regression suite (every scene, the release template against the editor build) exact
  with the new golden `ledge_corners`.
* The editor check passes.
* The laptop: not run (not linked to the session).

## 3. Measurements

A corner looked for costs about 4 µs of the CPU (GodotPhysics3D, cloud): a body held against a ledge's end with no
corner to go round looks again every 0.2 s, not every frame. A transfer's frames cost what a hang's do (the curve and
the hands; no queries).

## 4. Next

Drops to ledges below and diagonal jumps; clips for the corners and jumps; free climbing on surfaces without ledges.
