# 0232 — A garment's distance from the skin, a share a particle

Status: **KEEP** (`FellowCloth3D.skin_distance_mode`, `SKIN_DISTANCE_UNIFORM` by default: decision 0230's behaviour) ·
Branch `fellow/geometry` · Code: `modules/fellow_fluid/fellow_cloth_3d.{h,cpp}` (`_build()`, `_substep()`) · Checks:
`benchmarks/project/bench/cloth_check.gd` (lanes `garment_skin`, `garment_from_pins`, `garment_painted`), the regression
scenes `cloth` and `cape`.

## Problem

Decision 0230 keeps a skinned garment's free particles within `skin_max_distance` of their skinned places — one
distance for the whole garment. A skirt's hem should swing a hand's width from the legs while the cloth just under its
waistband stays on the hips: with one distance, the hem's freedom is the waist's too (a skirt flaring out under the
waistband in a gale), or the waist's tightness is the hem's (a skirt that cannot swing). Middleware paints the distance
a vertex (NvCloth's motion constraints' radii, Chaos Cloth's max distance mask); the readiness assessment's next step
for garments was "a garment's painted distance".

## Change

`skin_distance_mode` (with `follow_skin` and a `skin_max_distance`), each particle's share of the distance:

* `SKIN_DISTANCE_UNIFORM` (the default): all of it everywhere, as decision 0230.
* `SKIN_DISTANCE_FROM_PINS`: growing along the cloth from the pinned particles — each particle's shortest path at rest
  along the cloth's edges to a pin (the tethers' search), over the longest such path: none at a pin, all of it at the
  farthest particle. Nothing to paint: a skirt or a coat pinned at its top gets a waist on the body and a free hem.
* `SKIN_DISTANCE_VERTEX_COLOR`: painted in the `source_mesh`'s vertex colours' green, 0 to 1 (0: on the skin; welded
  vertices: the smallest), beside the pins' red. Without colours, a configuration warning and all of it.

The shares are found when the cloth is built; `skin_max_distance` still scales them at any time (0: the constraint off,
as before).

## Verification

`cloth_check.gd`'s skirt (decision 0230: 200 particles in ten rings of 20, its waistband pinned, on a skeleton circling
2.5 m with its hips swaying 20° and its spine twisting 25° against them, a capsule on the hips, in the flags' gale),
`skin_max_distance` 12 cm, the farthest each ring went from its skinned place over 4 s (cm; ring 0 is the waistband,
ring 9 the hem):

| ring | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| uniform (decision 0230) | 6.8 | 8.3 | 9.4 | 10.5 | 11.2 | 12.0 | 12.0 | 12.0 | 12.0 |
| from the pins (its limit) | 1.3 (1.3) | 2.7 (2.7) | 4.0 (4.0) | 5.4 (5.3) | 6.8 (6.7) | 8.0 (8.0) | 9.3 (9.3) | 10.7 (10.7) | 12.0 (12.0) |
| painted (green: the ring's place squared; its limit) | 0.1 (0.1) | 0.6 (0.6) | 1.3 (1.3) | 2.4 (2.4) | 3.7 (3.7) | 5.4 (5.3) | 7.3 (7.3) | 9.4 (9.5) | 12.0 (12.0) |
| nearest bones (decision 0184) | 5.6 | 6.4 | 8.6 | 10.6 | 12.0 | 13.2 | 14.7 | 16.3 | 18.0 |

Every ring within a millimetre of its limit (measured at the frame's pose, the limit kept against the substeps'
interpolated one). The mean distance a ring kept after the first two seconds: under the waistband 3.9 cm with one
distance, 1.2 cm growing from the pins, 0.1 cm painted; at the hem 9.6, 8.3 and 6.8 cm. The waistband on its skinned
place (0.0 mm) and nothing into the hips' capsule in every lane; the earlier lanes the same, the regression scenes
`cloth` and `cape` exact. Its cost: none measurable (a multiplication a particle in the substep).

## Decision

Keep, uniform by default: a garment's distance shaped along it — from its pins without painting, or painted — at no
cost.
