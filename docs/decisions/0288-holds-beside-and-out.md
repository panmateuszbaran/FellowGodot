# 0288 — Holds beside and out from the hands: the showcase's houses climbed to their roofs

Status: **KEEP** (all in `hang_reach_up`'s mode; nothing changes without it) · Branch `fellow/geometry` · Code:
`modules/fellow_animation/fellow_traversal_3d.{h,cpp}` (`_find_jump()`'s up branch, `_probe_edge()`, `_fit_hold()`,
`_hands_room()`, `_step_hang()`); `showcase/tools/blender/ac_buildings.py` (the collision), the rebuilt rooftops models
and scene · Checks: `bench/climb_check.gd` (`eave_deep`, `beside_reach`, `stacked_holds`) on GodotPhysics3D and Jolt, the
regression scenes `climb`, `ledge_corners` and `traversal`, the showcase's demo, a house of the showcase climbed.

## Problem

The showcase's houses could not be climbed past their first floor: the player sprinting at a front was caught by the
impost band or the rustication's top and stayed there, from every point along CasaCrema's front. Decisions 0282-0287
climbed lanes built for them; the houses showed what a real front asks:

* **The next hold is beside the hands, not over them.** A window's sill 0.9 m up, the hands under the pier between two
  windows: the reach up looked only straight up.
* **Holds close over each other.** The piano nobile's pediment base 0.4 m under the string course: the look for a top
  (a ray down from 0.3 m over the probe) began inside the string course and found nothing; a hold 0.4-0.5 m up had its
  face under the first probe (0.6 m).
* **The eave's lip is out from the wall.** From the last hood, the lip 0.6 m up and 0.7 m out: past the probe's reach
  out from the hold (0.65 m). The roof's collision was a surface with no face at the lip either (no fascia), and the
  pediments' bases, a hold on the visible stone, were not in the collision.
* **A hold where the hands do not fit**: on Jolt, a wall's top 1.5 cm under a roof was found (the ray down began inside
  the roof and Jolt reported the hit below); GodotPhysics3D did not.

## Change

In `hang_reach_up`'s mode (climbing from hold to hold):

* **Beside**: nothing over the hands within reach, a hold 0.3-0.9 m along the edge (the nearest first, the wish's side
  first), up to `hang_reach_up` higher, is reached up and across (`TRANSFER_REACH_SIDE`, with `hang_reach_side`);
  pushing up starts it.
* **Out**: a ledge 0.1 m to `hang_reach_up` over the hold and 0.3 m or more out from it (an eave's lip; looked for from
  the hold's face and from 0.4 m out) is reached up to, the body hanging out from the wall under it (decision 0287 then
  takes it over the lip).
* **Stacked**: the look for a top that begins inside a hold over the one probed is made again from 0.2 m over the face
  found; the probes up begin at 0.45 m (0.6 before).
* **The hands' room**: a hold needs 0.12 m free over its edge (`_fit_hold()`; 0.2 m to be caught from the air under an
  overhang, decision 0287).

The showcase's houses: the pediments' bases and the eaves' fascia (under the level edges) go into the collision; the
models and the scene are rebuilt (their visible meshes unchanged).

## Checks

`climb_check.gd`, three new lanes (`hang_reach_up` 1.25 m, `hang_brace_height` 0.65 m; `hang_reach_side` 0.8 m for
`beside_reach`):

| lane | | |
|---|---|---|
| `eave_deep` | a string course at 3.0 m, a roof of 21° over it, its eave 0.85 m out, the lip 0.75 m up | caught at 0.82 s; out and up to the lip (1.82 s); over it, standing on the overhang at 3.96 m |
| `beside_reach` | a string course at 3.0 m, a corbel 1.1 m up and 0.6 m to the left, the top 2.2 m up | caught; up and across to the corbel (1.87 s); up to the top (2.62 s), over it at 5.20 m |
| `stacked_holds` | a string course at 3.0 m, a pediment's base at 4.2 m, a string course 0.29 m over it, the top at 5.6 m | caught; up to the base (1.82 s), the course (2.57 s), the top (3.32 s); over it at 5.60 m |

No frame of the capsule in the world; the same events on Jolt (where `eave_deep` had hung from the wall's top under the
roof). The 29 lanes of milestone 151: value for value on both engines; the lanes of decisions 0285-0287 the same (the
eave's lip in `eave_roof` found 1 cm lower: its check's height 2.04 m against 2.05). The regression scenes: exact. The
showcase's demo: the same path.

**The showcase**: the player sprinting at CasaCrema's front, pushing on, from the street to its roof in 5 s: caught by
the impost band (2.2 m), a leap to the rustication's top (4.4 m), up and across to the first floor's sill (5.35 m), a
leap to the pediment's base (7.41 m), to the second floor's sill (8.75 m) and its hood (10.68 m), out and up to the eave's
lip (11.32 m, 0.85 m out), over it onto the tiles, and on over the ridge. Before: caught at 2.2 m, at 4.4 m at most.
CasaRosa: to its terrace's parapet (12.3 m); a terrace lower than the parapet's top is not climbed onto from a hang yet.

## Decision

KEEP.

## Rejected / not done

* **Over a parapet onto a lower terrace** from a hang (CasaRosa, CasaBianca): the room on the top is looked for at the
  top's height; next.
* **The palazzi's arcaded ground floors** (PalazzoOcra): no hold within a jump from the street between the shops'
  arches; their fronts need holds there.
