# 0200 — Transmission by frequency: a material that stops the highs and lets the level through

Status: **KEEP** (a collider's metadata `fellow_occlusion_highs`; without it the highs are blocked as the level, as
before) · Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_acoustics_3d.{h,cpp}` · Checks:
`benchmarks/project/bench/acoustics_check.gd` (a curtain lane, listened too; the others unchanged).

## Problem

Decision 0198's "Rejected": one share a material, the shelf following it. A curtain or a thin wooden door barely
lowers a sound but dulls it; heavy glass lowers it but keeps it bright.

## Change

A collider's metadata `fellow_occlusion_highs` (0 to 1) is how much it blocks a path's highs — the high shelf (up to
`occluded_shelf_db`) and the cutoff (down to `occluded_cutoff_hz`) — while `fellow_occlusion` blocks its level (up to
`occluded_volume_db`); without it the highs are blocked as the level. Both are added up along a path (decision 0199)
and eased alike; a path is traced on while either is not yet full. `get_occlusion_highs(player)`.

## Checks (cloud: the CPU, lavapipe)

`bench/acoustics_check.gd`, GodotPhysics3D and Jolt alike, the release template the same: a source 12 m away, 15° south
of east, behind a curtain at 9 m (`fellow_occlusion` 0.1, `fellow_occlusion_highs` 0.8):

| | occlusion (level, highs) | the mix (Movie Maker, noise) |
|---|---|---|
| through the curtain | 0.10, 0.80 | −11.0 dB against stock, its highs 0.46 → 0.03 |
| through the glass (decision 0198) | 0.30, 0.30 | −7.6 dB, its highs 0.46 → 0.22 |
| through the wall (decision 0186) | 1.00, 1.00 | −27.3 dB, its highs 0.62 → 0.02 |

The other lanes: the same values; the listened mix the same (the in-room source, measured after the curtain's step
now, 0.01 dB apart: its noise is other samples).

## Decision

Keep: materials can dull a sound without silencing it.

## Rejected / not done

* **A filter a material** (bands, resonances): the level and the highs, through the node's shelf and cutoff.
