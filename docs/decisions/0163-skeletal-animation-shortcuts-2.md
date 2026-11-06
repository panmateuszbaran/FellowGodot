# 0163 — Skeletal animation on the main thread: two more exact shortcuts

Status: **KEEP, on** (exact; the RTX's timing to come) · Branch `fellow/geometry` · Settings
`rendering/fellow/animation/shared_no_override_pose`, `animation/lazy_simulator_poses` · Code:
`scene/3d/skeleton_3d.{h,cpp}`, `scene/3d/physics/physical_bone_simulator_3d.{h,cpp}`, `fellow_settings.*` · Check:
`bench/animation_check.gd`.

## Problem

A profile of the slice's main thread without rendering (headless, `perf`, the release template with its symbol table)
puts its crowd's skeletal animation first: 48 characters, each an AnimationPlayer and a Skeleton3D of 52 bones with a
rotation track per bone. (The slice's "process time" of 5.3 ms on the RTX, f356, is not that thread's own work:
`Performance.TIME_PROCESS` is the largest of each second's main-loop steps, the RenderingServer's sync and draw
included, which with the default thread model are the frame's rendering and its waits for the GPU. Milestone 60's
stats split it: `main_process_usec`, `main_draw_usec`.)
Decision 0076 had removed the work that changed nothing in 2025's profile; Crowd Lab's profile now (128 characters,
release template) still showed, of the animation's and the skeletons' instructions:

* **The deprecated global pose without overrides.** Each skeleton update computes every bone's global pose and, for
  the deprecated `get_bone_global_pose_no_override()`, the same product again from the parent's copy: two
  `Transform3D` multiplications a bone where the skeleton has no override.
* **The compatibility simulator's copies.** Every Skeleton3D's PhysicalBoneSimulator3D (decision 0076) copies every
  bone's global pose after each update for physical bones it does not have: 5 % of Crowd Lab's main thread.
* **The key pair's trigonometry.** Every rotation track interpolates between two keys with `Quaternion::slerp()`:
  their dot product, `acos()` of it and its sine, then the weight's two sines. The first three depend only on the two
  keys, which stay the same for the frames between them (20 keys a second here, 60-144 frames).

## Change

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
same 12 hashes in 300 frames with the two off and on, with AnimationPlayers and with AnimationTrees blending idle,
walk and run (fractional weights).

The cloud's frame timings vary ±40 % between runs, so the cost is measured twice: the instructions executed in
`SceneTree::process()` (callgrind, release template, 48 characters, 120 frames: the same count every run), and the
process's CPU time (`perf stat -e task-clock`, release template, 128 characters, 1500 frames, six runs of each in
alternating order; the start's ~0.2 s, measured alone, taken off to give a frame's time):

| | instructions, AnimationPlayer | instructions, AnimationTree (blending) | CPU time a frame, AnimationPlayer | CPU time a frame, AnimationTree |
|---|---|---|---|---|
| both off | 553.3 M | 903.3 M | 1.85 ms | 3.20 ms |
| `shared_no_override_pose` alone | −4.9 % | −3.0 % | −2.7 % | −3.3 % |
| `lazy_simulator_poses` alone | −5.5 % | −3.4 % | −2.3 % | −2.6 % |
| **both** | **−10.4 %** | **−6.4 %** | **−7.3 %** | **−4.4 %** |

(Medians of six; the runs' standard deviation 1.5-3 %, 6 % for `shared_no_override_pose` alone with
AnimationPlayers, one of whose runs was 11 % slower. The frame here is the whole main thread: the bench's GDScript that
moves and seeks every character included.)

`lazy_simulator_poses` is exact for every physical bone except one bound and then simulated before the skeleton's next
update in the frame it was bound, while the bones' poses changed since the last update: its first transform is then
the pose of the current frame instead of the last update's.

## Rejected: the keys' part of the slerp kept per thread (`animation/slerp_key_cache`)

Built, exact (the same hashes), and fewer instructions — but slower. A per-thread table of the keys' sign, `acos()`
and sine, found by the first key's address and used only if both keys hold the same bits as when the slot was filled,
saved 2.9 % of the AnimationPlayer frame's instructions (4.2 % blending), yet with 16 384 slots (1 MB) the process
took 6.6 % more CPU time: 128 characters × 52 tracks touch 6 656 slots a frame scattered over the table, and each
miss in the processor's caches costs more than the `acosf()` and `sinf()` it saves (the profile put the function at
11.8 % of the main thread, from 3.9 % for `slerp()`). With 2 048 slots (96 KB) it was no faster than without
(2 704-2 876 ms against 2 734-2 789 ms, four runs each). Instruction counts alone would have kept it: the CPU time is
what decides.

## Decision

**KEEP, on.** A tenth of a crowd's animation instructions on the main thread go (6 % blending), 4-7 % of its CPU
time, with the same poses bit for bit. The RTX's timing (Crowd Lab and Slice Lab, the settings off against on) is job
f361's.

Not done: the track's key search and edge bookkeeping (253 instructions a track and frame, now the largest self cost),
`interpolate_via_rest()`'s normalization, animation LOD for AnimationPlayer crowds (not exact: a game's choice).
