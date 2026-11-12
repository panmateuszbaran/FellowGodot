# FellowEngine — milestone 95 report (a jump back to the wall behind)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0203-jump-back.md`.

## 0. Result

* **A jump back**: a character hanging on one wall leaps to a ledge on the wall behind it, turning round in the air
  (`hang_jump()` with the wish away from the wall) — alleys and shafts.
* **Checked**: a new lane on GodotPhysics3D and Jolt and the release template; the fourteen earlier lanes and
  decision 0180's course unchanged.

## 1. Development

`modules/fellow_animation/fellow_traversal_3d.{h,cpp}`, `doc_classes/FellowTraversal3D.xml`;
`benchmarks/project/bench/climb_check.gd` (one lane).

## 2. Verification

Decision 0203's table. The laptop (job f396, milestone 97): as expected with both engines, exactly the cloud's results;
filmed on the RTX.

## 3. Measurements

The search: one ray and four edge probes, once, when `hang_jump()` is called.

## 4. Next

Jumps round a corner in the air; kicking off a bare wall.
