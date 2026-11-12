# 0188 — Destruction that holds: broken where struck, standing while supported

Status: **KEEP** (behind `FellowDestructible3D.structural`, off by default: a destructible breaks whole as before) ·
Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_destructible_3d.{h,cpp}`,
`modules/fellow_editor/fellow_editor_plugin.{h,cpp}` ("Add Destructible Wall") · Checks:
`benchmarks/project/bench/structure_check.gd` (four lanes), `bench/destruction_check.gd` (decision 0182's thirteen,
unchanged), the regression scene `destruction`, the editor check.

## Problem

Decision 0182's objects break whole: a wall struck by a ball, or a pillar by a blow at its foot, turns entirely into
debris. The readiness assessment's physics row said "destruction without support graphs or cracks that hold": in a game
a hole is punched in a wall and the rest stands, a pillar cut at its foot falls in one piece, a wall cut across drops its
upper part.

## Change

* **Which pieces touch**: when a fracture's pieces are prepared (on the worker, or at ready when prefractured), the
  points of their surfaces are hashed on a grid of a thousandth of the object's size; two pieces that share three or
  more points (a face's corners — the cut points are computed alike for both cells) touch.
* **`structural`** (with `break_radius` 0.6 m and `anchor_height` 0.1 m), for a static object: at its first break the
  object stands in its pieces — each piece's mesh and convex shapes become children of the object's own body, where
  they were, the object's mesh hidden and its shapes off; a piece reaching within `anchor_height` of the object's lowest
  point stands on the ground. Each blow (a rigid body striking it, `damage()` to zero, `break_apart()`):
  * the pieces within `break_radius` of it (at least the nearest) break off, each a rigid body thrown out from the
    blow and by its impulse (a striking body's momentum); for 0.3 s they pass through the standing pieces (a chunk
    0.15 s): their faces lie on their neighbours', and touching all round they stayed wedged in the hole by friction;
  * from the pieces on the ground through the pieces that touch, what is still supported stands; each part that no
    longer reaches the ground becomes one rigid body (its pieces' meshes and shapes about its centre of mass) and falls.
  * The bodies it made do not strike it (its own debris would break it on and on), and a body that struck it strikes it
    again only after 0.25 s (going on through the hole it made). When nothing stands, the object is done
    (`free_after_break`).
* `get_standing_pieces()`, `get_structure()` (each piece's centre, bounds, whether it stands and is on the ground, the
  pieces it touches); `get_stats()` adds `standing`, `last_broken`, `last_fallen`, `last_chunks`. The FellowEngine
  menu's "Add Destructible Wall".

## Checks (cloud: the CPU, lavapipe)

`bench/structure_check.gd`, the editor build, each object prefractured and static on the ground; GodotPhysics3D and
Jolt alike:

| lane | expected | GodotPhysics3D | Jolt |
|---|---|---|---|
| a wall 4 × 3 × 0.25 m in 40 pieces, a 75 kg ball at 16 m/s left of its middle | a hole, the rest standing, no later breaks | 2 pieces out, 38 standing at 1 s and at 5 s | the same (the ball striking the hole's edge again: no more out) |
| a pillar 0.5 × 3 × 0.5 m in 14 pieces, broken at its foot | the rest falls together | 4 out at the foot, the other 10 one body, from 1.89 m to lying at 0.24 m | the same, 0.26 m |
| a wall struck four times across it at 1.4 m (charges) | each blow takes more; the upper part falls | 35, 31, 29, then 11 standing; 15 pieces one body, from 2.56 m down onto what stands (1.15 m) | 1.16 m |
| the same wall without the structural mode | all of it debris | 40 pieces, nothing standing | the same |

How it was found to work: the first version left the pieces knocked out of a hole where they had been — touching their
neighbours all round, they were held by friction (filmed: no hole to see); passing through the standing pieces while
they leave, they fly out (the hole filmed). The first cut (four blows of 200 N·s) left the upper part in place, resting
on pieces the weak blows had left in the gap; with charges it falls. A ball at 20 m/s struck the wall unseen on Jolt (a
static object sees a body up to 18 m/s the step before it strikes, decision 0182). Of a wall's 40 pieces 95 pairs touch
(a plane's Voronoi diagram of 40 cells has about 110 neighbouring pairs: the missing are edges shorter than the
tolerance, contacts of millimetres).

Decision 0182's thirteen lanes on both engines and the regression scene `destruction`: as before. The release
template: the same lanes. Lavapipe: the lanes filmed (the hole, the pillar's fall, the cut wall's upper part fallen
across what stands); the new regression scene `structure` (a wall with two holes blown in it, a pillar falling): the
same picture on every run and from the release template. The editor check: "Add Destructible Wall" without a warning
and undone (one start of the editor crashed before opening the project — a `std::system_error` from a thread, before
any FellowEngine node — and the rerun passed).

## Decision

Keep: a static object can now be broken a piece at a time where it is struck, its unsupported parts falling whole; the
whole-object break stays the default.

## Rejected / not done

* **Stress** (a part's weight on its supports, beams that sag and crack under load): support is connectivity to the
  ground, not strength.
* **Pieces glued to other objects** (a wall on a building's frame): only the object's own lowest pieces are on the
  ground.
* **Rigid objects in the structural mode** (a crate that loses a corner): a rigid body breaks whole as before.
