# 0189 — Round corners and across gaps: a hanging body goes on to other ledges

Status: **KEEP** (on `FellowTraversal3D`: `wrap_corners` on by default, `hang_jump()` called by the controller) ·
Branch `fellow/geometry` · Code: `modules/fellow_animation/fellow_traversal_3d.{h,cpp}` · Checks:
`benchmarks/project/bench/climb_check.gd` (ten lanes: decision 0183's six unchanged, four new),
`bench/traversal_check.gd` (decision 0180's course, unchanged), the regression scenes `climb` and `ledge_corners`.

## Problem

Decision 0183's hanging body stopped wherever its ledge ended: at an outside corner, at a wall meeting the ledge, at a
gap. Its "Rejected" named corners; a climbing game's ledges go round buildings and are broken by gaps the climber
jumps, and up a façade from ledge to ledge.

## Change

* **Round a corner** (`wrap_corners`, on): when the body, moving along the edge with the intent, stops (the edge ends
  or something is in the way), it looks for the ledge round the corner —
  * **inside** (a wall meeting the ledge): a ray along the edge from where it hangs, 12 cm in front of the face, meets
    a face turned back at it; the new edge on that wall's top, the body's width out from the corner;
  * **outside** (the wall turning away): from beyond the edge's end, 0.32 m behind the face, a ray looking back finds
    the wall's side; the new edge on it there —
  at the same height (within 0.3 m), both hands on it (the edge found 0.24 m either way), room for the capsule hanging
  from it. The body goes there in 0.5 s (inside) or 0.7 s (outside) along a curve (a quadratic Bézier, wide of an
  outside corner), turning to face the new wall, its hands moving from the old edge to the new and its feet on the
  wall. A look in vain is repeated after 0.2 s (one costs about 4 µs).
* **The wish held round the corner**: the intent is the controller's wish in the world; round a corner the same wish
  points away from the new wall (and would let go). Held as it was (within 45°), it now goes on along the new edge the
  way it went; changed, it is the world's again.
* **Jumps** (`hang_jump()`, with the wish's direction, at the next physics frame): **along** the edge past its end —
  the first edge found within `hang_jump_distance` (1.6 m) at about the same height (0.5 m), hung from 0.3 m into it,
  room there and half way (0.5 s, the curve 0.35 m up) — or **up** — a ledge within `hang_jump_height` (1.3 m) over the
  edge, both hands on it, room there and half way (0.45 s). The hands let go in the air (their weight down to 0.15) and
  catch the new edge. Nothing found: nothing happens.
* `is_transferring()`, the signal `hang_transferred(ledge, how)` (`TRANSFER_OUTER_CORNER`, `TRANSFER_INNER_CORNER`,
  `TRANSFER_JUMP_ALONG`, `TRANSFER_JUMP_UP`), the global stat `transfers`.

## Checks (cloud: the CPU, lavapipe)

`bench/climb_check.gd`, the editor build, a capsule 0.35 × 1.8 m with a test mannequin, 9 s at 60 fps; GodotPhysics3D
and Jolt alike, no frame of the capsule in the world in any lane:

| lane | expected | result |
|---|---|---|
| a free block 2.8 m high (2 × 1.6 m), hung from, the wish left | round the outside corner onto the block's side, on along it | round at 2.87 s, turned 90.0°, hanging 0.43 m off the side, 1.36 m along the edges |
| a 2.5 m wall with another meeting it from the front, the wish left | round the inside corner onto the other wall, out along it | round at 2.47 s, turned 90.0°, 0.43 m off the other wall, 1.92 m along |
| two 2.5 m ledges 0.8 m apart, the wish left, `hang_jump()` | across the gap, on to the next ledge's end | across at 3.22 s, stopped 0.21 m inside the far end |
| a 2.5 m ledge with a beam over it (its top at 3.6 m), under it, `hang_jump()` with the wish up | up to the beam | up at 4.42 s, hanging from 3.6 m |

Decision 0183's six lanes: the same numbers as before (1.35 m along the ledge and 0.21 m before its end, 0.21 m before
the gap, 120.5° round the tower at 2.03 m from its axis, the ladders and the climb); their ends are not wrapped: the
ledge behind the corner is 0.4 m deep with a wall rising behind it, where a hand finds no edge. Decision 0180's course:
as before. The release template: the same lanes.

How it was found to work: the first outside corner was found at 0.18-0.3 m behind the face and the body would have
wrapped decision 0183's 0.4 m ledges with one hand in the wall behind; both hands on the new edge is now required. A
first block 2.5 m high was mantled in the air (a low mantle: room on its top) — the lane's block is 2.8 m like the
climb lane's. A beam over the whole ledge was grabbed directly in the jump at the wall (the top found from above is
the beam's): the beam covers the ledge's left half, the body goes under it first. A stop at the end of the block's
side met the second corner at the moment the wish was let go; the wish is let go earlier.

Lavapipe: the lanes filmed (round the block, round the inside corner, across the gap, up to the beam); the new
regression scene `ledge_corners` (a body half way round an outside corner, another in the air over a gap: each
traversal stopped a set number of frames into its transfer) the same picture on every run and from the release
template; `climb` and the other scenes as before.

## Rejected / not done

* **Wall jumps away from the wall, ledges below** (a drop to a ledge under the feet), **jumps to ledges in other
  directions** (diagonal, across a corner): along and up only.
* **A moving ledge during a transfer**: the target is where it was when the transfer began.
* **Clips for the transfers** (a corner's or a jump's animation): the pose is the procedural one (hands moving,
  letting go), as the hang's.
