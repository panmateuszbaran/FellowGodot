# 0287 — Onto a roof over its eave

Status: **KEEP** (the eave in `hang_reach_up`'s mode; the sloping top in every mantle) · Branch `fellow/geometry` ·
Code: `modules/fellow_animation/fellow_traversal_3d.{h,cpp}` (`_check_ledge()`'s room on the top, `_hands_room()`, the
catch in the air) · Checks: `bench/climb_check.gd` (`eave_roof`) on GodotPhysics3D and Jolt, the regression scenes
`climb`, `ledge_corners` and `traversal`, the showcase's demo.

## Problem

In Assassin's Creed the climb ends on the roofs: from the last hold under the eave, up to its lip and over it onto the
tiles. FellowTraversal3D could not, three times over (a 4 m wall under a roof of 25°, its eave 0.45 m out, a corbel
0.9 m under the lip):

* **The hold under the eave was not caught.** From the air, a ledge that cannot be climbed onto is hung from — except
  where there is "no room above" it, which an eave 0.9 m over the corbel is: the check stands the body on the edge, and
  it does not fit under the eave. The hands need 0.2 m.
* (Hanging from the corbel, the reach up of decision 0282 found the eave's lip — its front a face, the tiles behind a
  top — and climbed to it, the body hanging out from the wall under it: no change needed.)
* **The roof was not climbed onto: "no room on top".** The room for the body on a top is a capsule set on it 3 cm up.
  On a slope the capsule's round end goes into the slope further up: a top sloping more than about 23° had no room on
  it, though `max_ledge_slope` (40°) allows it. The capsule over the edge had the same fault.

## Change

* **A hold under an eave** (in `hang_reach_up`'s mode: climbing from hold to hold): caught from the air where its hands
  have room — nothing within 0.2 m over its edge (`_hands_room()`, a ray) — though the body has none over it.
* **The room on a sloping top**: the capsule raised by its radius × (1 / cos(slope) − 1), its round end clear of the
  slope; the capsule over the edge at the slope's height there, raised the same. A flat top: the same capsules as
  before.

## Checks

`climb_check.gd`, a new lane `eave_roof` (`hang_reach_up` 1.25 m, `hang_brace_height` 0.65 m): a jump at the wall, then
pushing up:

| | |
|---|---|
| caught | the corbel at 3.0 m under the eave, at 0.82 s |
| up to the eave's lip (0.95 m) | hand over hand, at 1.82 s, the body out from the wall under it |
| over it | a high mantle, 1.83-3.02 s; standing on the roof's slope at 4.16 m, 2 cm in from the wall's face |

No frame of the capsule in the world; the same on Jolt. Before: no catch (with it, "no room on top" at the lip). The
other 31 lanes: value for value as before, on both engines (their tops are flat). The regression scenes `climb`,
`ledge_corners` and `traversal`: exact. The showcase's demo: the same path. A picture strip (lavapipe): hanging under the
eave, reaching up to its lip, over it, standing on the tiles.

## Decision

KEEP.

## Rejected / not done

* **An eave out of reach** (more than `hang_reach_up` over the last hold): leapt to as any ledge above, within
  `hang_jump_height`.
* **The showcase's houses**: whether their cornices under the eaves are within reach of a hold is the next check there.
