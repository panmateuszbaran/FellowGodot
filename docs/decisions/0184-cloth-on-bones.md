# 0184 — Cloth pinned to a skeleton's bones

Status: **KEEP** (two properties of `FellowCloth3D`; without them nothing changes) · Branch `fellow/geometry` · Code:
`modules/fellow_fluid/fellow_cloth_3d.{h,cpp}` · Checks: `benchmarks/project/bench/cloth_check.gd` (the lane
`cape_bones`), the regression scene `cape`; the laptop (job f395).

## Problem

Decision 0181's cloth hangs from its node: a cape on a character followed a `BoneAttachment3D` (one bone, a frame
behind it), so its pinned edge did not bend with the shoulders, and the readiness assessment's characters row said
"cloth hangs from nodes, not yet from a skinned character's bones".

## Change

`skeleton_path` (a `Skeleton3D`) and `pin_bones` (the bones allowed; empty: any). When the cloth is built or reset
(or the skeleton is set or changes), each pinned particle is bound to the nearest allowed bone — the segment from the
bone to its first child, as the skeleton is posed then — and its place in that bone's space is kept. Each frame, on the
main thread, the bound bones' global poses give each pin's place now; over the substeps the pins move from their last
frame's places to these (linearly; the free particles feel the motion as before). A cloth carried past
`simulation_distance` puts its pins on their bones. `set_particle_pinned()` binds again. The body: capsules on its
bones (`BoneAttachment3D`s with `CollisionShape3D`s) in `colliders`, read each frame as before. `get_stats()` counts the
`bone_pins`; a configuration warning names a skeleton path or a bone that is not there.

## Checks (cloud: the CPU, lavapipe)

`bench/cloth_check.gd`'s lane `cape_bones`: a skeleton (hips, spine, chest, neck) on a node circling 2.5 m at 1.6 m/s,
its chest twisting 35° each way twice a second; a cape 0.8 × 1.1 m (221 particles) pinned along its top edge to the
chest (13 bone pins), colliding with capsules on the hips and the chest; the scene's 7 m/s gusts at a quarter:

| | GodotPhysics3D | Jolt |
|---|---|---|
| the pins in the chest's space, at most moved | 0.02 mm | 0.02 mm |
| deepest into the body's capsules | 0 cm | 0 cm |
| mean strain at the end / largest seen | 1.3 % / 24 % (whipped by the twist) | the same |
| the simulation a frame (cloud, editor build) | 0.22 ms | 0.22 ms |

The first measurement compared the pins with the chest's pose of the frame after the cloth's (the bench moves the
skeleton in the physics frame, the cloth simulates in the frame's processing): 78 mm, the lag of one frame; measured
with the same frame's pose, 0.02 mm. The other ten lanes as in decision 0181. The regression scene `cape` (the body
walking an arc, its chest twisting, the cape on its chest): the same picture on every run.

## Results on the laptop (job f395, release template at 251ebff)

The eleven lanes with GodotPhysics3D and with Jolt, as on the cloud: the cape's pins 0.02 mm from their places in the
chest's space, nothing into the capsules, the mean strain at the end 1.35 % (26 % at the twist's whip); the
simulation a frame 0.25 ms (GodotPhysics3D) and 0.24 ms (Jolt) for its 221 particles, the flag's 400 0.31-0.34 ms.
Filmed on the RTX: the cape following the chest as it twists, the flag, the tablecloth over its table.

## Decision

Keep: the pins follow the bones they hang from, interpolated over the substeps like the node's transform before.

## Rejected / not done

* **The cloth's vertices skinned to the character's mesh** (cloth painted on a skinned mesh's vertices and blended with
  the skin): the cloth is its own mesh; a skinned garment's simulated part is a source mesh pinned by vertex colour to
  the bones.
* **Self-collision, tearing**: next, as in decision 0181.
