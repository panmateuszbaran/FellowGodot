# 0351 — The square's robes as cloth

Status: **KEEP** · Branch `fellow/geometry` · Code: `showcase/scripts/rooftops_crowd.gd` (`_skirt()`, `SKIRT_TOP`,
`SKIRT_HEM`, `SKIRT_FREE`, `SKIRT_SIMULATE`); `showcase/tools/climb_look.gd` (`--program=people`, the skirts' cloth
statistics).

## Problem

The owner: "the skirts not as solids but as cloth with a simulated material." A long robe on the square's people was a
cone (a cylinder mesh, 14 sides, one ring) fixed to the hips: it did not move with the legs, which went through it.

## Change

A robe is a `FellowCloth3D` on the figure's skin (milestones 116 and 147), no other engine work:

* a tube from the waist (1.0 m, 0.175 m round) flaring to below the knees (0.27 m, 0.31 m round), 18 by 9 — 162
  particles, 288 triangles;
* skinned in the skeleton's space: the waistband (vertex colour red) pinned, on the hips; down its length a growing
  share on the thigh of its side (0.7 of it at the hem), so that its shape at rest moves with the stride;
* each free particle kept within 0.22 m of its skinned place, its share growing from the waistband
  (`SKIN_DISTANCE_FROM_PINS`), never more than 1 cm behind it into the body (the backstop), and out of capsules on the
  thighs, the shins, the forearms and the hands (the arms swing past the hips);
* the scene's wind at 0.35, 240 substeps a second (a flag's 480 is not needed), half past 12 m from the camera,
  carried without simulating past 32 m; drawn both sides (its inside seen as it swings).

## Measurements

| | the cone (before) | **the cloth** |
|---|---|---|
| the legs through the robe walking | every stride | none seen but a hand's knuckles brushing it at the hip |
| a robe's simulation (a lavapipe machine, one figure walking) | — | 0.37 ms of a worker thread at 480 a second; half at 240 |
| the square's 14 robes and the town's other cloth, the RTX laptop's CPU | — | 1.0 ms of the main thread a frame, 5.0 ms of the workers' (all the cloth together) |
| the demo, the people's reactions | | unchanged (the player's path identical; a knocked-down figure's robe follows its ragdoll's hips) |

## Decision

KEEP.

## Rejected / not done

* The robe pinned to the hips bone alone (milestone 76's bone pins): with no skinned shape to stay near, the hem lags
  behind a walking figure and the legs push through it; on the skin it follows the stride.
* Self-collision: a robe this short does not fold onto itself walking.
