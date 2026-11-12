# FellowEngine — milestone 86 report (down to a ledge below)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0194-hang-jump-down.md`.

## 0. Result

* **`hang_jump()` with the wish down**: a hanging character drops to a ledge below and is caught there — down a
  façade ledge by ledge, completing milestones 81 and 84 (along, round, across, up, over an edge, down).
* **Checked**: a new lane (over a 6 m platform's edge into a hang, then down to a ledge 3.6 m high that stands out of
  the wall) on both physics engines and the release template, the earlier twelve lanes and decision 0180's course
  unchanged, filmed; the regression scenes `climb`, `ledge_corners`, `traversal` exact.

## 1. Development

* `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`: the search down, its path (out to the lower face, then
  down), `hang_jump_depth`, `TRANSFER_JUMP_DOWN`; `doc_classes/FellowTraversal3D.xml`.
* `benchmarks/project/bench/climb_check.gd`: one lane.

## 2. Verification

Decision 0194's table; the laptop: not run (not linked to the session).

## 3. Measurements

A search down: at most 21 edge probes (each two rays) and three capsule tests, once, when `hang_jump()` is called.

## 4. Next

Diagonal jumps and jumps to ledges on other walls.
