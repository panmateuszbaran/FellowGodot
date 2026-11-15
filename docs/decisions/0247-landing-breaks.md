# 0247 — The parts that fall break where they land

Status: **KEEP** (behind `FellowDestructible3D.landing_break_speed`, 0 by default: the parts land whole as in decisions
0188 and 0246) · Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_destructible_3d.{h,cpp}`
(`_track_chunk()`, `_check_chunks()`, `_break_chunk()`, `_finish_if_empty()`) · Checks:
`benchmarks/project/bench/landing_check.gd` (four lanes), the regression scenes `destruction`, `structure`, `stress`.

## Problem

A structural object drops what no longer reaches the ground as one rigid body (decision 0188), now also what its bonds
can no longer carry (decision 0246) — and that body stays whole wherever it lands: a pillar cut at its foot topples and
lies on the ground like a log, a balcony that breaks off falls 3 m and lands as a slab. Milestone 122's report: "the
parts fall whole: no breaking on landing".

## Change

* **`landing_break_speed`** (m/s; 0, off). Each part of several pieces the structure drops is watched (its contacts
  reported) until it comes to rest: a step whose change of velocity, less what gravity accounts for, passes the speed is
  a landing — as a rigid destructible's blow (decision 0182), but a speed, not an impulse: what breaks a falling part is
  the speed it stops from, not its mass (a heavy part would break on the gentlest landing by impulse). Where: the
  contact that pushed hardest along the change.
* **The break**: the part's pieces where they are now (the structure's mesh transform carried by the part's motion
  since it was made), the pieces within `break_radius` of the contact (at least the nearest) each a body, the rest split
  along the bonds between them that hold (decision 0246's, or every touching pair without a strength), each group a body
  — itself watched, so it can break again where it lands next. Every new body moves as its pieces did (the part's
  velocity at its centre, its spin), with the part's debris lifetime; `broken` is emitted with them.
* An object nothing of which stands kept its body until then and was freed with it (`free_after_break`): the node that
  watches its falling parts would have gone with them. Its body — empty: no shape, no mesh — now waits until its parts
  have come to rest.
* `get_stats()`: `falling_parts`, `last_landing_breaks`, `landing_breaks_total`.

## Verification

`bench/landing_check.gd` (the editor build; GodotPhysics3D and Jolt alike):

| lane | expected | GodotPhysics3D | Jolt |
|---|---|---|---|
| a pillar 0.5 × 3 × 0.5 m in 14 pieces broken at its foot (decision 0188's lane), landing at 3 m/s | its upper part breaks where it strikes the ground | 1 landing: the 10-piece part in 5 bodies (the largest 6 pieces) | the same |
| the same, 0 | lies whole | 1 part of 10 pieces, no landing break | the same |
| a balcony 2 × 0.25 × 1.5 m 3 m up on a building's wall, 300 kPa (it breaks off), 3 m/s | breaks where it lands | 19 pieces fall; 1 landing: 7 bodies, the largest 12 pieces | the same |
| the same, 10 m/s (it lands at about 7.7) | lies whole | 1 body of 19 pieces | the same |

Lavapipe: the lanes filmed (the pillar set to break lying in pieces where it struck, the other whole). The regression
scenes `destruction`, `structure` and `stress` the same as their goldens, from the release template too (the option
off by default).

How it was found to work: the first runs saw no landing — the parts' bodies did not report contacts (Godot's contact
monitor is per body; it is turned on for the parts watched) — and the pillar's node was freed with its empty body
before its part landed. A wall's part over a hole was the first lane: it settles about 0.1 m onto the stubs beside the
hole, no landing at all; the balcony's free fall is the lane now.

## Decision

Keep: a structure's falling parts break where they land, as hard as they land, opt-in by a speed.

## Rejected / not done

* **An impulse threshold** (mass × change of velocity, as a rigid destructible's): a part of 1 t breaks on any landing
  a part of 10 kg survives; the speed it stops from is what the material feels.
* **The stresses of the landing** (the impact through the part's bonds, decision 0246's solver with the contact
  force): the pieces near the contact break off and the rest splits along its bonds — the landing's force is not
  carried through the part.
