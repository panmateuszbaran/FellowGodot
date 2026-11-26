# 0357 — The hay's straws stir in the wind

Status: **KEEP** · Branch `fellow/geometry` · Code: `showcase/scripts/rooftops_hay.gd` (the straws' shader);
`tools/climb_look.gd` (`--hay_turn`).

## Problem

Milestone 210's open point: the cart's 26,000 straws did not move. Since milestone 211 the square has its weather on
the keys — a blizzard's 14 m/s past a hay cart whose straws stood still.

## Change

The straws' shader takes FellowEngine's wind (decision 0125: a material with `render_mode fellow_wind`, its
`WIND_HEIGHT` / `WIND_BEND` / `WIND_FLUTTER` set in its vertex function, is swayed by the scene's `FellowWind3D` — its
speed, direction and gusts — in the vertex stage, shadows and motion vectors included): each straw a stem of its own
length (the blade is 1 m along +Y, scaled per instance; `WIND_HEIGHT` 1) bending from its foot; how far it bends by how
upright it is, up or down (`WIND_BEND` 0.04 lying in the heap — held by the others — to 0.6 standing out of it or
hanging over the boards); a little flutter (0.4).

## Measurements

The RTX, 1280x720, the cart from 4.2 m with the camera still (`--hay_turn=0`), 60 frames: the heap's middle (half the
frame's height and 40 % of its width), the pixels changed between two frames.

| | mean change per frame (levels) | changed by more than 12 levels |
|---|---|---|
| blizzard (14 m/s), before (f605: the shader set the wind's values without `render_mode fellow_wind`, which they need) | 3.3 | 1.3 % (the snow falling) |
| blizzard, after (f608) | 6.4 | 13.0 % |
| the fair evening (3.5 m/s), after | 6.5 | 12.7 % |

Over 2 s the blizzard's heap changes by 13.2 levels on average, the fair evening's by 10.8 (the straws' sway grows with
the square of the wind's speed, softly limited). The demo's path unchanged (a material's vertex stage).

## Decision

KEEP.
