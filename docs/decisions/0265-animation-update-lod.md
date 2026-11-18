# 0265 — Animation update rate by distance (far characters animate less often)

Status: **KEEP** (opt-in: `rendering/fellow/animation/update_lod`, off) · Branch `fellow/geometry` · Settings
`rendering/fellow/animation/update_lod`, `update_lod_distance`, `update_lod_max_interval`,
`update_lod_offscreen_interval`, `update_lod_offscreen_margin` · Code: `scene/animation/animation_mixer.{h,cpp}`
(`_fellow_update_lod_skip()` in the internal process notifications), `servers/rendering/fellow_settings.{h,cpp}` ·
Checks: `benchmarks/project/bench/animation_lod_check.gd` (new), `bench/animation_check.gd` (manual seeks unchanged),
Crowd Lab `--crowd_process=1`, the image regression suite.

## Problem

Every animated character costs the main thread every frame, however small on screen: its AnimationPlayer or
AnimationTree evaluates its clips' tracks, its Skeleton3D computes its bones' global poses and hands them to the
RenderingServer, and the render thread and GPU skin its meshes again. Decisions 0076, 0163 (exact shortcuts) and 0164
(process thread groups) made each evaluation cheaper or moved it off the main thread; a crowd still evaluates all of
its characters every frame: Crowd Lab's 256 characters 3.1-3.4 ms of the main thread (readiness review: "Crowd Lab's
256 characters in 8 groups cost the main thread a third"). Engines that ship AAA crowds update far or off-screen
characters' animation less often (an "update rate optimisation": every 2nd, 4th, 8th frame by distance or screen
size), a character 60 m away moving 15 times a second instead of 60 — not something a player sees.

## Change

`rendering/fellow/animation/update_lod` (off): an AnimationMixer (AnimationPlayer, AnimationTree) processed every frame —
the idle or the physics callback, not manual (`advance()`, `seek()` are the game's own calls) — whose root node is a
Node3D, and that has no root motion track (a game moves its character by the motion each frame), evaluates:

* every frame within `update_lod_distance` (20 m) of its viewport's camera, every 2nd frame within twice that, every
  4th within four times, …, at most every `update_lod_max_interval`-th (8); the distance scaled by the camera's field of
  view (for 75°: a wider view sees a character smaller, a zoom larger);
* every `update_lod_offscreen_interval`-th frame (8) while its root node is outside the camera's frustum by more than
  `update_lod_offscreen_margin` (2 m: a body around its feet);
* on its own frames of the interval (a hash of the mixer: a crowd's evaluations spread over them, not all on one);
* a skipped frame's time added to its next evaluation: its clips at the same times as without the setting, their
  method and audio tracks fired then, `animation_finished` on time to within the interval.

Nothing is evaluated on a skipped frame — no track, no bone pose, no skeleton update, no skin transforms handed to the
RenderingServer (whose skinning then has nothing new to do). Off, or in the editor, the mixer is processed as before.

## Verification

`animation_lod_check.gd` (headless: the dummy renderer, the main thread's work; `--fixed-fps 60`): Crowd Lab's 256
characters playing their clips in the idle process, spread over a field 120 m square with a camera at its edge looking
across it (some of them out of view); every evaluation's clip position checked against its phase plus the frames'
time. `animation_check.gd` (Crowd Lab's manual seeks): the same hashes with the setting off and on. The image
regression suite (exact: off by default). Crowd Lab `--crowd_process=1` on lavapipe (480 × 270, its three
cameras): the near characters the same pixels, the far ones in their last evaluated pose — at eye level 2 603 of
129 600 pixels differ, all on the characters beyond 20 m; from the air 6 193 (most of the crowd beyond 20 m); close up
669 (the background's characters).

## Results

The scene's process step (the main thread's animation and skeletons; the cloud's CPU, three runs each interleaved, the
editor build):

| 256 characters | evaluations a frame | the busiest frame's | the main thread's process step |
|---|---:|---:|---:|
| setting off | 256 | 256 | 3.36-3.47 ms |
| **`update_lod`**, a field of 120 m | **49.75** | 52 | **1.15-1.18 ms (−66 %)** |
| a field of 40 m (most within 20-40 m) | 128.5 | | 3.52 → 2.13 ms (−39 %) |
| a field of 240 m | 36.5 | | 3.48 → 1.03 ms (−70 %) |

On the 120 m field (4 characters within 20 m, 14 within 40 m, 54 within 80 m, 104 within 160 m, 80 out of view):
evaluated every frame, ½, ¼, ⅛ and ⅛ of the frames; every evaluation put its clip where it is without the setting
(the largest difference 2·10⁻¹⁴ s). Manual seeks unchanged (the same hashes off and on).

**Crowd Lab on the RTX** (milestone 143, `m143_f488_crowd_update_lod`: its 256 characters playing in the idle process,
`--crowd_process=1`, the flythrough at 1080p, the release template, 3 runs each interleaved, autotune off): the frame
4.38 → **2.99 ms (−32 %)**, its 99th percentile 6.4 → 4.4 ms, the GPU's 1.74 → 1.77 ms (the skinning of the same
characters, their poses updated less often) — the scene is the main thread's, and the far crowd's animation was most
of it.

## Decision

Keep, opt-in: a crowd's far and hidden characters cost a fraction of the main thread, the near ones nothing changes;
a game that needs every character's pose every frame (a far sniper's target, a cutscene) leaves it off or uses manual
processing for those.

## Rejected / not done

* **Interpolating the skipped frames' poses** (from the last two evaluations): smoother far motion for the cost of a
  pose blend every frame — most of what the skip saves.
* **By screen size from the meshes' bounds**: the root's distance with the field of view is what the mixer knows
  without asking the renderer; a giant at 100 m updates as a man at 100 m.
* **Skeleton modifiers** (IK, look-at, physical bones: `SkeletonModifier3D`s): their skeleton processes them every
  frame on its own callback, on the poses its mixer last set; a skeleton with active modifiers saves the mixer's
  evaluation only. Throttling them too would change what a game's IK targets (a foot on moving ground) see.
