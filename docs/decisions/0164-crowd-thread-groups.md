# 0164 — Crowds on worker threads: process thread groups

Status: **KEEP** (a scene's choice: nothing changes unless a scene puts its characters in groups; exact) · Branch
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

On the RTX laptop (24 cores; release template, 1080p, frame-locked, interleaved, six runs each; jobs f361, f361b):

| | the scene's process a frame | the frame (median) |
|---|---|---|
| Crowd Lab, 256 characters, AnimationPlayers, on the main thread | 3.07 ms | 4.79 ms |
| the same in 8 groups | **1.04 ms (−66 %)** | **3.19 ms (−33 %)** |
| in 24 groups | 1.09 ms | 3.23 ms |
| AnimationTrees blending, on the main thread | 5.25 ms | 7.09 ms |
| the same in 8 groups | **1.40 ms (−73 %)** | **3.32 ms (−53 %)** |
| Slice Lab's 48 characters, on the main thread → 8 groups | 0.664 → 0.490 ms (−26 %) | 4.497 → 4.555 ms (the GPU's frame) |

With the characters in groups the crowd's frame is the GPU's and the presentation's (2.6-2.7 ms of GPU): 24 groups
gain nothing over 8. The slice's frame was already the GPU's; its main thread gains a quarter.

## Decision

**KEEP** (as a way to build a crowd; a scene's choice). A crowd of 256 animated characters takes a third of the main
thread it took, the same poses bit for bit. Not done: grouping automatically (a mixer may animate anything in the
tree; Godot's groups are the scene's statement that its nodes are independent).
