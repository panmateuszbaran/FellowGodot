# 0164 — Crowds on worker threads: process thread groups

Status: **measuring** (a scene's choice: nothing changes unless a scene puts its characters in groups; exact) · Branch
`fellow/geometry` · Code: `benchmarks/project/scenes/crowd_lab.gd` (`--crowd_groups`), `crowd_group.gd`,
`slice_lab.gd` (`--slice_crowd_groups`) · Check: `bench/animation_check.gd --groups=N`.

## Problem

Decisions 0076 and 0163 took out of a crowd's skeletal animation the work whose result was known; what is left is
the animation itself — every track's keys interpolated, every bone's pose and global pose, every skin's transforms —
about 13 µs a character and frame on the cloud's CPU (Crowd Lab headless, 128 characters, 1.67 ms a frame), and all
of it on the main thread, one character after the other. Godot 4 can process a part of the scene tree on worker
threads (`Node.process_thread_group`: a sub-thread group's nodes are processed by the WorkerThreadPool in parallel
with the other groups of the same order, and their deferred notifications flushed on the same thread), but
FellowEngine's benchmarks never did, and nothing showed that FellowEngine's animation code gave the same poses there.

## Change

Crowd Lab `--crowd_groups=N` (Slice Lab `--slice_crowd_groups=N`): the characters split into N group nodes
(`PROCESS_THREAD_GROUP_SUB_THREAD`, order 1: after the main thread's group, whose script sets the time). Each group's
`_process()` poses its characters — the root's place, the AnimationPlayer's seek or the AnimationTree's advance — and
the skeletons' updates (`NOTIFICATION_UPDATE_SKELETON`, a deferred thread-group notification) run on the same worker
thread; their RenderingServer calls are queued for the server's thread, as any thread's are. Without the option the
scene is as before.

## Verification

* `animation_check.gd` (editor build, 48 characters, 300 frames): with 2 and 8 groups the same 12 hashes of every
  bone's pose rotation, global pose and global pose without overrides as on the main thread, with AnimationPlayers
  and with AnimationTrees blending three clips; every FellowEngine animation setting on; no error.
* Crowd Lab captured on lavapipe (editor build, 32 characters, three cameras): with 4 groups 0 pixels differ.
* The cloud's two vCPUs (release template, headless, 128 characters, two runs each): the scene's process step
  1.66-1.71 ms a frame on the main thread, 1.43-1.51 ms with 2 groups, 1.55-1.62 ms with 4; the process's CPU time
  +16 % with 2 (the two vCPUs seem to share a core: the same functions take ~20 % longer each).

To measure on the RTX laptop's 24 cores (job f361): Crowd Lab with 256 characters, 8 and 24 groups, AnimationPlayers
and AnimationTrees; Slice Lab with its 48 characters in 8 groups.

## Decision

Pending the laptop's measurement.
