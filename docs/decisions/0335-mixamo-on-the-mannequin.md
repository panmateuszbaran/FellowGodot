# 0335 — Mixamo's motion capture on the rooftops' mannequin

Status: **KEEP** (the showcase only; nothing in the engine changes; the clips are not in the repository) · Branch
`fellow/geometry` · Code:
* `showcase/tools/blender/mixamo_retarget.py` (new): Mixamo's FBX onto the mannequin's skeleton, the clips cut and
  exported as one .glb with `clips.json`;
* `showcase/tools/build_mocap_clips.gd` (new): the .glb as the player's library, `showcase/rooftops/mocap/` (ignored by
  git: `showcase/.gitignore`);
* `showcase/scripts/rooftops_player.gd` (`_use_mocap()`, `_variant()`, the gaits' table `_gaits`, `_one_shots`);
* `showcase/tools/anim_quality.py` (a walk with no right contact inside a left stride reported missing, not a crash).

## Problem

The owner, playing the rooftops: "the jump and many other animations look very unrealistic". The mannequin's clips are
drawn by code (`mannequin_anims.py`, milestones 143-160): poses keyed by hand, gaits from a biomechanical model,
checked by `anim_quality.py`'s 38 gates — plausible, not alive; the jump crouched while the body already rose. Asked
whether a technical demo's animations could be used (Epic's Game Animation Sample: "UE-Only Content", for Unreal
alone), the owner chose Mixamo's motion capture (Adobe's: free, a game may use its clips, its files may not be handed
out) and, its downloads needing his Adobe account, signed in himself in the built-in browser; the clips were then
downloaded from Mixamo's site by hand (its API from the page was refused: it would have read his session's token).

## Change

* **The clips** (Mixamo, on its "Knight D Pelegrini", FBX without skin, 30 fps, no key reduction, in place where
  offered): Breathing Idle → `idle`; Standard Walk → `gait_walk`; Slow Run → `gait_jog` (Mixamo's "Jogging", 1.6 m/s,
  was barely faster than its walk); Running (with intention) → `gait_run`; Running (forward quickly) → `gait_sprint`;
  Jump (in place) → `jump`; Jump (forward running) → `jump_run`; Falling Idle → `fall`; Falling To Landing → `land`;
  Hard Landing → `land_hard`; Falling To Roll → `roll`. Kept by the owner in `.tools\mixamo`, never committed.
* **The retarget** (`mixamo_retarget.py`, Blender's FBX importer through the `bpy` module): each frame every mapped bone
  (42: the spine, the limbs, two phalanges a finger) turns in the world as Mixamo's turns from its rest pose (both rest
  in a T-pose facing −Y; a source facing +Y turned); the hips move as Mixamo's scaled by the hips' heights; the legs
  reach by two-bone IK for Mixamo's ankles scaled the same way (the ankles' rest heights matched: the shins' lengths
  differ), the knees towards Mixamo's, the feet and toes turned as Mixamo's. Stick figures of both at eight moments of
  every clip (`MIXAMO_DEBUG`).
* **The cuts**: a gait loops as Mixamo's does (one cycle, or a shorter period that repeats), started at the left foot's
  strike (its ankle furthest forward of the hips), played over a second, its speed the planted foot's sliding back under
  the hips (walk 1.51 m/s, jog 3.02, run 3.95, sprint 4.69; cycles 1.17, 0.73, 0.70, 0.70 s); a loop that is not a
  cycle (the idle, the fall) has its last 0.4 s crossfaded into its start; a jump runs from 0.12 s before its flight
  (the longest stretch with both ankles off the ground) to 0.25 s past its apex; a landing from its touchdown. The
  hips stay in place: a loop's drift (a straight line) and a one-shot's travel (half a second's average) taken out, and
  a jump's flight height — the player's body rises and falls.
* **The player** (`rooftops_player.gd`): where `res://rooftops/mocap/mocap_clips.res` exists, its clips replace the
  mannequin's of the same names in the player's own copy of the library (the crowd keeps the mannequin's); the gait
  blend space's points move to the mocap's speeds and the cycle follows their table (faster than the sprint's 4.69 m/s
  its stride over a shorter cycle); a jump at more than 3 m/s plays the running jump, a fall of more than 3 m the hard
  landing (held 0.9 s). Without the library nothing changes.

## Measurements

* The retarget: the mannequin's joints within a few centimetres of Mixamo's scaled ones through every clip (the stick
  figures); its ankles stand at 8.2-9.1 cm in stance (its own rest: 8.5).
* `anim_quality.py` (headless, the rooftops' player): **30 of 38 gates** with the mocap (38 with the mannequin's own).
  Steady gaits: the feet slide 0.3-1.5 cm a stance (median), the right foot strikes at 0.49-0.51 of the left's stride.
  What fails:
  * the walk's knee 34° at loading (5-20°), 29.5° at toe-off (30-45°), the ankle 6.3° plantarflexed at loading
    (10-25°) — Mixamo's walk carries its knees bent; with its 1.17 s cycle the gate's 3.3 s saw one left stride (its
    left-right phase missing);
  * the run's and sprint's least knee flexion 11° and 5.5° (the gate: 15°) — the captured runner straightens the knee
    at push-off;
  * the stop: a foot slides 19 cm (5); the starts from standing 15-70 cm (not gated; the mannequin's own: 2-22): the
    gaits mixed while accelerating or braking put a foot planted in one clip in the air of the other;
  * the jump at a wall (the mannequin's own clip) out of a mocap sprint: the arms at 2,720°/s (2,200).

## How it got there

* Mixamo's "Jogging" (1.63 m/s, 0.83 s cycle) sat beside its walk: the walk's gates were measured mostly on the jog.
  "Slow Run" (3.02 m/s) instead.
* The jumps first kept Mixamo's flight: the hips rose a metre over the player's own rise. The flight's height out;
  the running jump's takeoff found as the longest flight, not the first (a run's strides fly too).
* The roll (no "in place" on Mixamo) travelled forward over the player's own motion: a one-shot's travel taken out.
* Gait cycles from the mixed strides (the blend space's weights): the starts and the stop slid more. Mixing two gaits
  only in the middle third of the speeds between them: the starts slid up to 78 cm, the jump at a wall popped more. A
  quarter-second crossfade between states: no change. The mannequin's own idle with the mocap's gaits: a start from it
  slid less (15 cm), a run's start more (78) — the mocap's idle kept.

## Decision

KEEP: the mocap where the owner has it; the eight gates' failures stand as measured — the steady motion is captured
motion, the transitions between gaits are what is left: start and stop clips (Mixamo's "Start Walking", "Running To
Stop"), and the hangs and climbs (their second batch).

## Rejected / not done

* Epic's Game Animation Sample, Unity's samples: licensed to their engines.
* Mixamo's files, or what is made from them, in the public repository; archives of Mixamo's clips elsewhere (copies
  handed out against its licence).
* Retargeting by bone rolls or local rotations: the two skeletons' bones are oriented differently; world deltas from the
  rest poses need no table of rolls.
