# FellowEngine — milestone 84 report (over an edge into a hang)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0192-drop-to-hang.md`.

## 0. Result

* **`FellowTraversal3D.try_drop()` and `drop_to_hang`**: a character walking slowly over the edge of a roof or a
  platform hangs from its edge instead of falling off — out over it and down, turning to face the wall, the hands
  catching the edge — then shimmies, goes round corners, jumps or lets go as milestones 75 and 81 made it; running, it
  goes over. Off by default.
* **Checked**: two new lanes on GodotPhysics3D and Jolt, the earlier ten and decision 0180's course unchanged, the
  release template the same, filmed; the regression scenes `climb`, `ledge_corners`, `traversal` exact.

## 1. Development

* `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`: the edge search, the drop (a transfer of its own: out, then
  down), the wish that walked it over held to nothing, `drop_to_hang`, `drop_max_speed`, `try_drop()`,
  `TRANSFER_DROP`; `doc_classes/FellowTraversal3D.xml`.
* `benchmarks/project/bench/climb_check.gd`: two lanes.

## 2. Verification

* Decision 0192's table, editor build and release template, both physics engines.
* Lavapipe: the lanes filmed; the regression subset (`climb`, `ledge_corners`, `traversal`, `face`) exact against the
  goldens and the release template.
* The laptop: not run (not linked to the session).

## 3. Measurements

With `drop_to_hang` on, a body walking slowly with an intent casts one ray a frame (the floor ahead found: done); at
an edge, four rays and two capsule tests more.

## 4. Next

Dropping from a hang to a ledge below; a turn-round clip at the edge.
