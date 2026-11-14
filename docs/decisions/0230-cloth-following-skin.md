# 0230 — Cloth following a skinned mesh's skin

Status: **KEEP** (`FellowCloth3D.follow_skin`, off by default) · Branch `fellow/geometry` · Code:
`modules/fellow_fluid/fellow_cloth_3d.{h,cpp}` (`_build()`, `_bind_skin()`, `_skin_places()`, `_bind_bones()`,
`_update_bone_pins()`, `_substep()`) · Checks: `benchmarks/project/bench/cloth_check.gd` (lanes `garment_skin`,
`garment_bones`), the regression scenes `cloth` and `cape`.

## Problem

Decision 0184 pins a cloth's particles to a skeleton's bones, each to its nearest bone, rigidly. A garment's attached
edge — a skirt's waistband, a coat's collar — sits where the body's skin blends two or more bones (hips and spine,
spine and chest): the skinned body moves between them, the pins move with one, and a gap or an overlap opens between
the garment and the body as the bones turn against each other. And a garment's free part, thrown by a turn or the wind,
can go anywhere the fabric reaches. Decision 0184 left "the cloth's vertices skinned to the character's mesh" not done;
the readiness assessment's characters row said the cloth "is its own mesh (not a skinned garment's vertices)".
Middleware drives garment cloth from the skin: the animated mesh's places are the pins' and, by a painted maximum
distance, the free particles' leash (NvCloth's motion constraints, Chaos Cloth's max distance).

## Change

`follow_skin` (off), `skin_max_distance` (m, 0: anywhere), `skin` (a `Skin`, optional):

* **The skin's data**: a `source_mesh` with bone weights (`ARRAY_BONES`/`ARRAY_WEIGHTS`, four or eight a vertex: a
  skinned mesh's surface, in the skeleton's space as Godot imports one) gives each particle its first vertex's binds and
  weights (a seam's vertices share them; of eight, the heaviest four, normalized).
* **The binds**: the `Skin`'s bones (by name, or index) and bind poses; without one, bind *i* is bone *i* with the
  inverse of its global rest — as a `MeshInstance3D` without a skin is skinned. A bind's bone missing: the pins fall
  back to decision 0184's nearest bones (a configuration warning).
* **Each frame** (the main thread, with the bone pins): each bind's matrix (the skeleton's global transform, the bone's
  global pose, the bind pose) and each particle's skinned place by linear blend skinning; last frame's kept, so that the
  substeps interpolate between them.
* **The pinned particles** follow their skinned places (interpolated over the substeps as bone pins are); **the free
  ones** are kept within `skin_max_distance` of theirs (after the cloth's own constraints, before the colliders).
  Bound (the cloth built or reset, the skeleton set, `follow_skin` turned on), every particle starts at its skinned
  place: the garment starts on the body. Past `simulation_distance` the cloth is skinned (its LOD: the garment mesh).
* `get_stats()`: `skin_particles`, `skin_limited` (free particles at the distance in the last substep).

## Checks (cloud: the editor build and the release template the same)

`cloth_check.gd`'s new lanes: a skirt (an open cylinder, 0.26 m round, 1.05 m down to 0.5 m; 200 particles) made in
GDScript with bone weights — its waistband half to the hips, half to the spine, and pinned (vertex colour); the rest to
the hips — on a skeleton circling 2.5 m at 1.2 m/s, its hips swaying 20° and its spine twisting 25° against them once
a second, a capsule on the hips, in the flags' gale. Following the skin (12 cm), and pinned to the nearest bones as
decision 0184 does. The same mesh on a hidden `MeshInstance3D` on the skeleton, skinned by Godot
(`bake_mesh_from_current_skeleton_pose()`, with a renderer: not headless):

| | the waistband from its skinned place (LBS in GDScript) | from Godot's skinning of the mesh | the farthest free particle from its skinned place | into the hips |
|---|---:|---:|---:|---:|
| `garment_skin` (`follow_skin`, 12 cm) | 0.0 mm | 0.02 mm | 12.0 cm | 0 |
| `garment_bones` (decision 0184) | 51.5 mm | 51.49 mm | 17.98 cm | 0 |

The earlier lanes the same (bit for bit, timings aside), the regression scenes `cloth` and `cape` exact. The skinning
runs on the main thread, four bone transforms a particle a frame (not timed apart; the skirt's simulation on its worker
0.32 ms a frame against 0.29 ms pinned to bones); with no skin data or `follow_skin` off nothing runs.

## Decision

Keep, off by default: a garment's attached edge moves with the body's skin, and its free part stays about the body.

## Rejected / not done

* **A painted distance** (a maximum distance per vertex, a colour channel): one distance for the cloth; a garment that
  needs a stiff part and a loose one is two cloths, or the stiff part pinned.
* **Backstops** (a particle kept outside the skin along its normal): the body's colliders do it for capsules.
* **Dual-quaternion skinning**: Godot skins linearly; the cloth follows the mesh as Godot draws it.
* **The skinning on the worker threads**: the bones' poses are read on the main thread; the skinning itself could move
  to the worker with the matrices — not needed at a garment's size.
