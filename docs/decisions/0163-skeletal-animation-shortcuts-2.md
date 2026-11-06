# 0163 — Skeletal animation on the main thread: three more exact shortcuts

Status: **KEEP, on** (exact; the RTX's timing to come) · Branch `fellow/geometry` · Settings
`rendering/fellow/animation/slerp_key_cache`, `animation/shared_no_override_pose`, `animation/lazy_simulator_poses` ·
Code: `scene/resources/animation.{h,cpp}`, `scene/animation/animation_mixer.cpp`, `scene/3d/skeleton_3d.{h,cpp}`,
`scene/3d/physics/physical_bone_simulator_3d.{h,cpp}`, `fellow_settings.*` · Check: `bench/animation_check.gd`.

## Problem

The slice's main thread is its frame's longest part on the RTX (its process time 5.3 ms a frame, f356), and a profile
of it without rendering (headless, `perf`, the release template with its symbol table) puts its crowd's skeletal
animation first: 48 characters, each an AnimationPlayer and a Skeleton3D of 52 bones with a rotation track per bone.
Decision 0076 had removed the work that changed nothing in 2025's profile; Crowd Lab's profile now (128 characters,
release template) still showed, of the animation's and the skeletons' instructions:

* **The key pair's trigonometry.** Every rotation track interpolates between two keys with `Quaternion::slerp()`:
  their dot product, `acos()` of it and its sine, then the weight's two sines. The first three depend only on the two
  keys, which stay the same for the frames between them (20 keys a second here, 60-144 frames).
* **The deprecated global pose without overrides.** Each skeleton update computes every bone's global pose and, for
  the deprecated `get_bone_global_pose_no_override()`, the same product again from the parent's copy: two
  `Transform3D` multiplications a bone where the skeleton has no override.
* **The compatibility simulator's copies.** Every Skeleton3D's PhysicalBoneSimulator3D (decision 0076) copies every
  bone's global pose after each update for physical bones it does not have: 5 % of Crowd Lab's main thread.

## Change

* `animation/slerp_key_cache`: a rotation track's linear interpolation keeps, per thread, the keys' part of the slerp
  (the sign of the second key, `acos()` of the dot product and its sine, or the near-equal case) in a table of 16 384
  slots found by the first key's address. A slot is used only if both keys hold the same bits as when it was filled
  (an edited or moved key is computed again); the weight's part is computed every time with `slerp()`'s own
  expressions and types (a sine in double precision, the other in single). Debug builds keep `slerp()`'s
  normalization checks on every miss. Read through the mixer's per-frame shortcut flags (decision 0076).
* `animation/shared_no_override_pose`: when no bone has a global pose override this update, and the two global poses
  of every bone held the same bits after the last full update (or every bone is computed again now), each bone's
  global pose without overrides is a copy of its global pose: the same product of the same operands, by induction from
  the roots. The lazy path of `get_bone_global_pose()`, which updates only the global pose, ends the equality until
  the next full update.
* `animation/lazy_simulator_poses`: the simulator of a skeleton without physical bones marks its copies stale instead
  of making them; they are made when a physical bone is bound (the cache's rebuild), or when one is read or written.

## Verification

`bench/animation_check.gd` (headless; Crowd Lab's characters stepped by 13.1 ms, not a multiple of the keys'
spacing; every 25 frames a hash of every bone's pose rotation, global pose and global pose without overrides): the
same 12 hashes in 300 frames with the three off and on, with AnimationPlayers and with AnimationTrees blending idle,
walk and run (fractional weights).

Instructions executed in `SceneTree::process()` (callgrind, release template, 48 characters, 120 frames; the cloud's
timings vary ±40 % between runs, the instruction counts not at all):

| | AnimationPlayer | AnimationTree (blending) |
|---|---|---|
| all three off | 562.9 M | 921.8 M |
| `slerp_key_cache` alone | −2.9 % | −4.2 % |
| `shared_no_override_pose` alone | −4.8 % | −2.9 % |
| `lazy_simulator_poses` alone | −5.4 % | −3.3 % |
| **all three** | **−13.1 %** | **−10.4 %** |

Of that, the animation's evaluation (`AnimationPlayer::seek`) 301.1 → 285.1 M (−5.3 %) and the skeletons' updates
220.5 → 162.8 M (−26 %); the rest of the frame is the bench's GDScript, unchanged. The slerp's saved instructions are
`acos()` and `sinf()`, whose latency weighs more than their count: the time saved should be larger than −2.9 %.

`lazy_simulator_poses` is exact for every physical bone except one bound and then simulated before the skeleton's next
update in the frame it was bound, while the bones' poses changed since the last update: its first transform is then
the pose of the current frame instead of the last update's.

## Decision

**KEEP, on.** A tenth to an eighth of a crowd's animation work on the main thread goes, with the same poses bit for
bit. The RTX's timing (Crowd Lab and Slice Lab, the settings off against on) is job f361's.

Not done: the track's key search and edge bookkeeping (253 instructions a track and frame, now the largest self cost),
`interpolate_via_rest()`'s normalization, animation LOD for AnimationPlayer crowds (not exact: a game's choice).
