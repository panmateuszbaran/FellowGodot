# 0198 — What a wall is made of: occlusion by material

Status: **KEEP** (a collider's metadata `fellow_occlusion`; without it a blocked path counts fully, as before) ·
Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_acoustics_3d.cpp` · Checks:
`benchmarks/project/bench/acoustics_check.gd` (a glass lane, with `--listen=1` the mix through it; decision 0186's
lanes unchanged).

## Problem

Decision 0186's occlusion counted every blocked path alike: a pane of glass, a curtain and a concrete wall muffled a
sound the same. Its readiness row: "occlusion without materials".

## Change

A ray from the listener to a source that hits a collider with the metadata `fellow_occlusion` (a float, 0 to 1) adds
that much to the source's blocked share instead of 1 (glass 0.3, a curtain 0.15, a thin wooden wall 0.6). The level,
the high shelf and the cutoff follow the share as before (−12 dB, −18 dB, 1 kHz at a full wall). The first collider on
a path decides.

## Checks (cloud: the CPU, lavapipe)

`bench/acoustics_check.gd`, GodotPhysics3D and Jolt alike: a source 12 m north-east behind a pane at 9 m (its
`fellow_occlusion` 0.3), beyond the moving source's circle:

| | occlusion | gain | the mix (Movie Maker, noise) |
|---|---|---|---|
| through the glass | 0.30 | −3.6 dB | −7.6 dB against stock (the shelf on the highs too), its highs 0.46 → 0.22 |
| through the wall (decision 0186) | 1.00 | −12 dB | −27.3 dB, its highs 0.62 → 0.02 |

Decision 0186's lanes (open, wall, past an edge, in a car, moving, the rooms; the listened four and the room's
tail): the same values as before.

## Decision

Keep: walls, glass and curtains muffle as much as they should, by a metadata on their colliders.

## Rejected / not done

* **Several walls on a path** (each one's share added): the first collider decides.
* **Transmission by frequency** (glass passing lows, cutting highs more than a curtain): one share a material, the
  shelf following it.

## Later

Milestone 91 (decision 0199): up to three colliders on a path add up; this record's lanes are unchanged.
