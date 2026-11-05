# FellowEngine — milestone 48 report (motion matching)

Status: **measured on the cloud's CPU and lavapipe** (2026-11-04; no GPU work, the RTX not used). Branch
`fellow/geometry`. Decision record `docs/decisions/0149-motion-matching.md`.

## 0. Result

A new module, `modules/fellow_animation`: `FellowMotionDatabase` (clips sampled into a searchable database, BVH import)
and `FellowMotionMatcher3D` (a `SkeletonModifier3D` that animates a skeleton by motion matching). On 27 minutes of the
CMU motion capture database's locomotion, 16 characters wandering a plaza walk, jog, run, turn, start and stop with no
state machine or blend space authored, follow their simulated path within **3.9 cm**, slide their feet **0.11 m/s**
while in contact (0.32 m/s without foot locking and IK) and blend their transitions (popping's 99th percentile **36 mm**,
174 mm with hard cuts), for **15 µs a character** a frame; 256 characters take 4.5 ms a frame, 3.0 ms with animation LOD.

## 1. Development

* **The database** (`fellow_motion_database.{h,cpp}`): poses at 60 Hz, a ground root facing the pelvis's way, foot
  contacts, 27 normalised features per frame (feet, hips, the trajectory 1/3-1 s ahead), a search pruned by bounding
  boxes over 16 and 64 frames; static bones dropped and rotations in 16 bits (116.5 → 35.9 MB); saved and loaded as a
  resource; `import_bvh()` and `create_skeleton()` for BVH motion capture.
* **The matcher** (`fellow_motion_matcher_3d.{h,cpp}`, `fellow_motion_math.h`): a spring-driven simulation and its
  predicted trajectory, a search every 0.1 s (and on input changes and clip ends), inertialization of every bone and
  the root's velocity, the root's motion pulled towards the simulation and clamped, foot locking with two-bone IK, LOD
  by camera distance, `advance_group()` on the WorkerThreadPool, statistics (sliding, trajectory error, popping
  percentiles, search times).
* **Matching Lab** (`--scene matching`): the plaza and steering of Motion Lab with mannequins skinned to the BVH
  skeleton; `bench/matching_eval.gd` measures without rendering; `scripts/get_cmu_locomotion.sh` fetches the data.

## 2. Verification

* The characters on lavapipe: walking, jogging, running, turning and stopping in the plaza's sequences.
* Deterministic: two runs, and the worker threads against the main thread, give the same statistics.
* Two faults found by the statistics and fixed: short clips' clamped trajectories (walking matched running), and the
  IK's knee axis flipping on straight legs (a 382 mm knee jump; popping's p99 56 → 36 mm).

## 3. Measurements

16 characters, 60 s at 60 Hz, headless on the cloud's CPU:

| | foot sliding in contact | character to simulation | popping p99 | transitions /s | update |
|---|---:|---:|---:|---:|---:|
| **all on** | **0.109 m/s** | 3.85 cm | **35.8 mm** | 4.9 | 14.8 µs |
| no locking or IK | 0.324 m/s | 3.85 cm | 31.3 mm | 4.9 | 14.5 µs |
| hard cuts | 0.111 m/s | 3.57 cm | 174.3 mm | 4.6 | 14.0 µs |
| no synchronization | 0.069 m/s | 31.5 cm | 38.3 mm | 5.1 | 14.8 µs |

Searches: 27.5 µs median, 92.5 µs at the 99th percentile, 191 of 98 109 frames checked on average. 256 characters:
4.52 ms a frame, 2.98 ms with LOD at 10 and 25 m, 3.26 ms of wall time with `advance_group()` on two cores. The
database: import 2.6-2.9 s, build 0.8-0.9 s, load 15-49 ms.

## 4. Limits

* Flat ground: no rays to adapt the feet to slopes and steps.
* Locked feet still slide 0.064 m/s where the IK cannot reach (the root drifting from the stance foot).
* Locomotion only, no tags (crouching, carrying, combat), no mirrored clips; the skeleton faces +Z and its transform
  relative to the moved root is taken as identity.
* LOD switches the IK on and off at its first distance (popping's p99.9 rises to 145 mm in the LOD run).
* No AnimationTree node: the matcher is a skeleton modifier (other modifiers, such as Godot's IK nodes, run after it).
* Timings above are the cloud's shared CPU. On the laptop (milestone 49's job f336, Windows): the matchers' wall time a
  step 0.12 ms for 16 characters, 0.25 ms for 64, 0.89 ms for 256 on the worker threads (0.73 ms with LOD, 7.0 ms on
  the main thread alone); Matching Lab at 1080p on the RTX 1.37 ms a frame with 16 characters, 1.88 ms with 64.

## 5. Next

* Ground adaptation, synchronization that waits for the swing, mirroring and tags; an AnimationTree node.
* The agreed order continues with platforms (Godot's exports in containers and emulators), then tools.
