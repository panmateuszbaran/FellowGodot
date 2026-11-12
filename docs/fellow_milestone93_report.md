# FellowEngine — milestone 93 report (diagonal jumps)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0201-diagonal-jumps.md`.

## 0. Result

* **Diagonal jumps**: a hanging character jumps across a gap to a ledge higher or lower (`hang_jump()` with the wish
  along and up or down) — with milestones 81, 84, 86 a climber reaches the next ledge in any direction on a wall.
* **Checked**: a new lane (across a 0.8 m gap, 0.9 m up) on GodotPhysics3D and Jolt and the release template; the
  thirteen earlier lanes and decision 0180's course unchanged.

## 1. Development

`modules/fellow_animation/fellow_traversal_3d.{h,cpp}`, `doc_classes/FellowTraversal3D.xml`;
`benchmarks/project/bench/climb_check.gd` (one lane).

## 2. Verification

Decision 0201's table. The laptop: not run (not linked to the session).

## 3. Measurements

The search: at most about 170 edge probes (two rays each) and two capsule tests, once, when `hang_jump()` is called.

## 4. Next

Jumps to ledges on other walls, wall jumps.
