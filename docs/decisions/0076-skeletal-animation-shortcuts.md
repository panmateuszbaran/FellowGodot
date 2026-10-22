# 0076 — Skeletal animation on the main thread: exact shortcuts

Status: **KEEP**, on by default from `fellow168` · Branch `fellow/geometry` · Settings `rendering/fellow/animation/elide_modifier_backup`,
`animation/full_weight_slerp`, `animation/track_shortcuts`, `animation/key_hints` (from `fellow170`) · Code: `scene/3d/skeleton_3d.cpp`,
`scene/3d/skeleton_modifier_3d.*`, `scene/3d/physics/physical_bone_simulator_3d.*`, `scene/animation/animation_mixer.cpp`,
`scene/resources/animation.*`, `fellow_settings.*`.

## Problem

Crowd Lab (new in this milestone: 256 characters, each with its own AnimationPlayer and Skeleton3D, 52 bones, one
rotation track per bone) is limited by its main thread: on the benchmark machine 5.2-5.9 ms per frame for stock and
FellowEngine alike, of which the render thread's part is 0.5-0.9 ms and the GPU 1.6-2.1 ms (`m15_f222`, `m15_f224`).
The separate render thread does not help (the main thread is the frame). Every frame each player evaluates its clip's
13 312 tracks and poses its skeleton, each skeleton computes its bones' global poses and hands the skinning transforms
to the RenderingServer.

A profile of the scene's main-thread work (headless, the dummy renderer, `perf` on a build with its symbol table)
found three kinds of work that change nothing:

* **The modifiers' backup.** Every Skeleton3D has a modifier in builds with the deprecated API: the compatibility
  PhysicalBoneSimulator3D, active (`animate_physical_bones` is on by default) even when the skeleton has no physical
  bone. Because a modifier exists, each update saves every bone (pose, its cache, the global pose) and the global-pose
  dirty flags before the modifiers run and restores them after the skins are updated: 12 % of the frame, for a modifier
  whose processing touches no bone.
* **Slerp at weight 1.** `Animation::interpolate_via_rest()` blends each track's value into the bone with
  `Quaternion().slerp(rest⁻¹ · value, weight)`. With one animation playing the weight is exactly 1, and slerp still
  computes `acos()`, a `sin()` in double precision, two `sinf()` and two divisions to get the coefficients 0 and 1.
* **Per-track bookkeeping.** Each track's interpolation runs a binary search for the last key not past the animation's
  length (a constant of the track) before the search for the current time; each rotation and scale key goes through
  `post_process_key_value()`, two Variant conversions, which returns it unchanged unless a script overrides it.

## Change

* `animation/elide_modifier_backup`: when every modifier of a skeleton reports that its processing changes nothing this
  frame (`SkeletonModifier3D::fellow_processing_is_noop()`: inactive with full influence; for the PhysicalBoneSimulator3D
  also: full influence, no bone with a physical bone, no connection to `modification_processed`), the modifiers are
  processed without the save and the restore.
* `animation/full_weight_slerp`: at weight 1, slerp's coefficients are exactly +0 and 1 on both of its paths
  (`sin(0.0) / sin(ω)`, and `sin(1 · ω) / sin(ω)`: the same `sinf()` of the same value; or `1 − 1` and `1`), and the
  result is computed with those coefficients in slerp's own expressions (signed zeros included; the engine is built
  with `-ffp-contract=off`).
* `animation/track_shortcuts`: the last key's index without the binary search when no key but the last can match the
  length (the search would only move up, to the last key); rotation and scale keys not passed through
  `post_process_key_value()` once it is known that no script overrides it; consecutive bone tracks of one skeleton look
  it up once in `_blend_apply()` (only `set_bone_pose_*()` runs between them; any other kind of track forgets it). The
  two settings are read once per mixer and frame (`GLOBAL_GET_CACHED` per track cost 7 % of the frame by itself).
* With `elide_modifier_backup` the simulator keeps its count of bones with a physical bone (updated where they are
  bound and unbound) instead of looping over its bones each update, and skips its own loop when the count is 0.

Tried and not kept: copying each bone's global pose into its `pose_global_no_override` (the deprecated API's value)
when the skeleton has no override. In stock that value lags: it is computed only by the full update, not by
`get_bone_global_pose()`'s lazy path, so it differs from the global pose in every bone of an animated skeleton; the
copy changed what `get_bone_global_pose_no_override()` returns.

## Verification

Headless, Crowd Lab with 64 characters, 200 frames (times between frames not a multiple of the clips' key spacing):
every bone's global pose, global pose without overrides and pose rotation of every character hashed every 25 frames —
**the same hashes** with each setting off and on and all together (also with `fellow168`'s additions). lavapipe, Crowd Lab captures (3 cameras), demote diagnostics, autotune off,
all off against all on: **0 differing pixels**.

Headless timing (Xeon, 2 cores; 256 characters, 600 frames, the frame limiter off): 5.2-5.5 ms → 4.0-4.3 ms with
all three (backup −10-15 %, slerp −11 %, track shortcuts a few % more); on a second container 12.1 → 9.4-9.9 ms.

RTX machine, `fellow167` (`m15_f225`):

In one process (blocks of 30 frames, autotune off), Crowd Lab's frame (limited by its main thread):

| settings | frame | against all off |
|---|---:|---:|
| all off | 5.487 ms | |
| `elide_modifier_backup` | 5.216 | −4.9 % (p 2e-21) |
| `full_weight_slerp` | 5.215 | −5.0 % (p 3e-18) |
| `track_shortcuts` | 5.160 | −6.0 % (p 3e-23) |
| all three | **4.620** | **−15.8 %** (p 2e-207) |

Between processes (3 runs each): stock 5.99 ms, `fellow167` (off) 6.11, `fellow167` with the three on **5.10**
(1 % low 111 / 116 / **131** fps). The render thread and the GPU do not change.

`fellow168` (with the simulator's count and the skeleton looked up once; `m15_f226`): in one process all off against
all on **−19.6 %** (5.458 → 4.386 ms, p 3e-222); the warm `crowd_pan` sequence off against on **bit for bit** (99 dB,
max |d| 0); between processes stock 5.25 ms against `fellow168` **4.60** (−12.3 %; 1 % low 117 → 132 fps).

`fellow170` adds `animation/key_hints` (on): each track's key found in the previous frame, then the next one, is
tried before the binary search for the current time, and taken only when it and the following key enclose the time
strictly, both outside the search's tolerance — then no key matches and the search would return that one (the keys
are sorted). Headless hashes identical with steps of 0.029 s, 1/60 s, 0.05 s (landing on the keys) and 0.1 s, and
with `--crowd_blend=1` (AnimationTrees blending two clips with fractional weights). RTX (`m15_f228`, `fellow170`): in
one process key hints off against on **−4.9 %** (4.403 → 4.187 ms, p 2e-21); the warm sequence with every animation
setting off (0076's and 0077's) against the defaults **bit for bit**.

## Decision

**KEEP, on by default from `fellow168`.** Exact (the same bone poses bit for bit; captures and the RTX sequence unchanged),
Crowd Lab's frame 16-20 % faster. Scenes without skeletal animation do not run this code.
