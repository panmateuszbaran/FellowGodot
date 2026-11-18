# 0279 — A jump round a corner from a hang

Status: **KEEP** (on, as the other jumps: only `hang_jump()` makes it) · Branch `fellow/geometry` · Code:
`modules/fellow_animation/fellow_traversal_3d.{h,cpp}` (`_find_jump()`: the corner after the along search;
`TRANSFER_JUMP_CORNER`; the wish's remap after it) · Checks: `bench/climb_check.gd` (`hang_jump_corner`) on
GodotPhysics3D and Jolt, the regression scenes `climb` and `ledge_corners`.

## Problem

Hanging, the body goes round a corner where its ledge goes on round it (decision 0189, `wrap_corners`), and jumps along,
up, down, diagonally and back to other ledges (decisions 0189, 0194, 0201, 0203). A ledge round the corner that does
not join this one — on the side of a building, set back from its front; a ledge standing out of the side wall — was
out of reach: the jump along looks on along the same line, the wrap needs the ledge to go on. The readiness assessment
listed it: "no jumps round a corner in the air".

## Change

`hang_jump()` with the wish along the edge and no edge found along it within `hang_jump_distance`: from the edge's end
(the corner), points back along the side of the wall — 0.3 m to `hang_jump_distance` from the corner, every 0.1 m —
are probed for a ledge facing out of the side (its face looking the way the body was going, the probe a little in
front of the side's plane, so that a ledge standing out of it is found), at about this edge's height (0.5 m) and with
both hands on it (±0.24 m). The body flies there in 0.6 s through a point out round the corner (beyond the edge's
end and in front of the face), turning a quarter as on a corner's wrap; room for the body there, at that point and
between. The wish held as it was goes on along the new edge (the stick held left stays "left"), as after a wrap.

## Checks

`climb_check.gd`'s `hang_jump_corner` (the editor build, GodotPhysics3D and Jolt the same): a 2.5 m ledge 2 m long on
the front of a 6 m building; on the building's left side, 1 m back from the ledge's front, a 2.5 m ledge standing out
0.4 m, 1.2 m long. Hung from the front one, along it to its left end, `hang_jump()` with the wish left: round the corner
(`hang_transferred: corner`), turned 90.0°, hanging 0.43 m off the side ledge's face, along it; no frame of the capsule
in the world. The other 24 lanes the same as before; the regression scenes `climb` and `ledge_corners` exact.

## Decision

KEEP.

## Rejected / not done

* **A corner the wall turns into** (an inside corner across a gap): the wrap's inside corner covers the joined case;
  a gap there is a jump along onto the other wall, which the along search does not look for — later work.
