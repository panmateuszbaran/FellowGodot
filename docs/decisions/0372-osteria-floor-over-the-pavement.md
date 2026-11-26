# 0372 — The osteria's floor over the pavement's slab

Status: **KEEP** · Branch `fellow/geometry` · Code: `showcase/scripts/rooftops_osteria.gd` (`FLOOR_LIFT`).

## Problem

The owner, in the osteria: "the floor flickers here" — the room's terracotta floor flickered with grey flagstones in
patches as the camera moved. The raised pavement along CasaRosa's front (`build_rooftops.gd`'s `_pavements()`: a
slab round each building, its footprint and 1.2 m more, its top at the kerb's 0.08 m) runs on under the house; the room's
floor (milestone 213) was laid at the pavement collider's top, `FLOOR` = 0.08 m — the same plane. Two coplanar surfaces:
the depth test took one or the other by the pixel and the camera's jitter.

## Change

The floor's tiles drawn 6 mm over `FLOOR` (`FLOOR_LIFT`), the arches' sill edges up to it; the colliders as they were
(the people's feet 6 mm in the tiles, unseen).

## Decision

KEEP.
