# 0149 — Motion matching: clips searched every tenth of a second, blended by inertialization, feet locked with IK

Status: **KEEP** (new module `modules/fellow_animation`; nothing changes without its nodes) · Branch `fellow/geometry`
· `FellowMotionDatabase`, `FellowMotionMatcher3D` (`fellow_motion_database.{h,cpp}`, `fellow_motion_matcher_3d.{h,cpp}`,
`fellow_motion_math.h`) · Bench: `scenes/matching_lab.gd` (`--scene matching`), `bench/matching_eval.gd`,
`scripts/get_cmu_locomotion.sh`.

## Problem

A Godot character moves with clips an AnimationTree blends by hand: a blend space of walk and run cycles, a state
machine for starts, stops and turns. Every transition is authored, the feet slide wherever the blend's speed or the
turn does not match the clip's, and the result is as good as the graph someone built. AAA locomotion has used motion
matching for a decade (Ubisoft's For Honor, 2016; The Last of Us Part II): the clips are a database, and the pose that
best continues the current one towards where gameplay wants to go is found every few frames. The assessment
(`docs/fellow_aaa_readiness.md`) lists motion matching, IK and animation LOD as the animation gaps; neural motion
(decision 0143) generates locomotion on the GPU from NVIDIA's model, but its weights and sample clips are NVIDIA's, for
evaluation of NVIDIA's technology only.

## Change

**The database** (`FellowMotionDatabase.build(skeleton, animations, options)`): every frame of a set of `Animation`
clips sampled at 60 Hz — each bone's local pose, a root on the ground under the hips facing the way the pelvis faces
(the right hip's joint to the left's, Gaussian-smoothed over 0.15 s), the hips relative to that root, the feet's
contacts (a toe below its 5th-percentile height plus 4 cm and slower than 0.3 m/s), and per frame 27 features: both
feet's positions and velocities, the hips' velocity, and the root's trajectory 1/3, 2/3 and 1 s ahead (positions and
facing directions on the ground), in the root's space, normalised per group (mean, mean standard deviation) and
weighted (D. Holden's defaults: 0.75, 1, 1, 1, 1.5). A frame whose trajectory would run past its clip's end is not
searched. Search: the nearest frame by squared distance, starting from the current frame's own cost, pruned by bounding
boxes over 16 and 64 frames, each distance abandoned as soon as it exceeds the best. Storage: bones that never move are
not stored, rotations in 16 bits; the database saves as a resource. `import_bvh()` reads Biovision BVH files into
`Animation`s (channels in any order, end sites as bones), `create_skeleton()` builds their `Skeleton3D`.

**The matcher** (`FellowMotionMatcher3D`, a `SkeletonModifier3D`): gameplay sets `desired_velocity` (and
`desired_facing`); a simulated character follows them with critically damped springs (half-lives 0.27 s). Every
`search_interval` (0.1 s), when the desired velocity changes by 0.1 m/s or when the clip ends, the query — the current
frame's pose features and the simulation's predicted trajectory relative to the character — is searched; a better
frame is blended in by inertialization (the offset between the old and new poses, and their velocities, decays with a
0.1 s half-life, per bone and for the root's velocity). The character's root moves with the animation's root motion,
pulled towards the simulation (at most half the root's speed, a 0.1 s half-life) and never farther than 0.15 m from it.
Feet in contact are locked where they landed until they lift or the animation pulls them 0.2 m away, then blended
back; two-bone IK reaches the locked position (the knee's axis kept on the side knees bend to, a soft limit in the last
3 % of the leg's reach), the ankle keeps its animated orientation. `lod_distances`: beyond the first, searches half as
often and no IK; beyond the second, a quarter as often and a full update at a quarter of the rate (the root carrying on
with its velocity in between). `advance_group()` runs many matchers' updates on the WorkerThreadPool and moves their
roots afterwards. Statistics: searches and their time (percentiles), transitions, foot sliding while in contact, the
character's distance from the simulation, popping (per frame the largest second difference of the hips' and legs'
joints), the last search's features.

Found on the way: frames whose trajectory was clamped at a short clip's end look like stopping, so every request to
slow down matched the last second of a run, and the current pose kept the character in running clips (walking at
1.3 m/s played jogs; 18 % of the feet's frames in contact instead of 57 %): only frames with a whole trajectory are
searched, and the first frame is found by the trajectory alone. The IK's first knee axis (the animated knee's side of
the hip-ankle line) flipped on nearly straight legs: popping's 99th percentile 56 mm, a 382 mm jump of a knee; with the
axis kept forward and the soft limit, 36 mm.

## Verification

Matching Lab (`--scene matching`): 16 characters wandering a plaza (walkers, joggers, runners and idlers going to
random waypoints, swerving round each other, stopping) on the CMU Graphics Lab Motion Capture Database's locomotion
(subjects 16, 35 and 69: 158 clips, 27 minutes; "free for use in research and commercial projects worldwide",
fetched by `scripts/get_cmu_locomotion.sh`, not part of the repository). On lavapipe the characters walk, jog, run,
turn and stop (`seq-matching_close`, `seq-matching_walk`). The same run twice, and with `advance_group()` on the worker
threads against each matcher on the main thread: the same statistics (the updates are deterministic and independent).

## Results

`bench/matching_eval.gd`, headless, 60 s at 60 Hz after 2 s, on the cloud's CPU:

| 16 characters | foot sliding in contact | of which locked | character to simulation | popping p99 / p99.9 | transitions /s | update | search (p50 / p99) |
|---|---:|---:|---:|---:|---:|---:|---:|
| **matching, inertialization, locking and IK** | **0.109 m/s** | 0.064 | 3.85 cm | **35.8 / 68.8 mm** | 4.9 | 14.8 µs | 27.5 / 92.5 µs |
| without locking and IK | 0.324 m/s | — | 3.85 cm | 31.3 / 57.8 mm | 4.9 | 14.5 µs | 27.5 / 97.5 µs |
| hard cuts (no inertialization) | 0.111 m/s | 0.062 | 3.57 cm | 174.3 / 199.8 mm | 4.6 | 14.0 µs | 27.5 / 82.5 µs |
| the root following the animation alone | 0.069 m/s | 0.046 | 31.5 cm | 38.3 / 75.8 mm | 5.1 | 14.8 µs | 27.5 / 92.5 µs |
| searching every 0.05 s | 0.115 m/s | 0.064 | 4.06 cm | 36.8 / 68.3 mm | 6.3 | 20.7 µs | 27.5 / 87.5 µs |
| searching every 0.2 s | 0.113 m/s | 0.064 | 4.09 cm | 35.3 / 68.3 mm | 4.2 | 12.8 µs | 27.5 / 92.5 µs |

A search checks 191 of the 98 109 frames on average (the boxes prune the rest); its 99.9th percentile is 148 µs, its
worst 0.37-5.6 ms (the full scan's cost, on a shared CPU).

| crowd | update (mean / p99) | the matchers a frame | with LOD (10 m, 25 m) | on the worker threads (2 cores) |
|---|---:|---:|---:|---:|
| 16 | 14.8 / 77.5 µs | 0.24 ms | — | — |
| 64 | 15.5 / 77.5 µs | 0.99 ms | — | — |
| 256 | 17.7 / 87.5 µs | 4.52 ms | **2.98 ms** (10 / 45 / 44 % of the updates at LOD 0 / 1 / 2) | 4.22 → **3.26 ms** of wall time |

The database: 158 clips imported from BVH in 2.6-2.9 s, built in 0.8-0.9 s, 35.9 MB (116.5 MB with every bone and full
floats), saved in 0.2 s, loaded in 15-49 ms.

## Decision

**KEEP.** Characters move by matching 27 minutes of motion capture with no state machine, transition or blend space
authored: they follow their simulated path within 4 cm, turn, start and stop, their feet slide a third as much as the
matched animation alone (0.11 against 0.32 m/s while in contact), and transitions blend (popping's 99th percentile
36 mm against 174 mm with hard cuts), for 15 µs a character a frame — 256 characters in 3 ms with LOD. Next: the
remaining sliding while locked (the IK out of reach when the root drifts from the stance foot: synchronization that
waits for the swing, stride warping), ground adaptation (rays to uneven ground), mirrored clips, tags (crouching,
carrying), an AnimationTree node for it, the RTX's CPU for the timings, and a comparison with neural motion
(`FellowMotionCharacter3D`) on the same character once a shared rig is licensed for it.
