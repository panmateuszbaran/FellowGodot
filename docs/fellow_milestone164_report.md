# FellowEngine — milestone 164 report (motion matching's AAA layer)

Status: **FINAL** (2026-11-20). Branch `fellow/geometry`. Everything ran in the cloud (CPU; the film on lavapipe); the
laptop's GPU stays paused.

## 0. Result

The owner, of the rooftops showcase: "the animations are not nice, the graphics are far from AAA, we must work on the
engine itself". He chose animation first, without the GPU.

The showcase's clips are made by code: that is content, not the engine. The engine's motion matching runs on real
motion capture (the CMU database). Decision 0305 adds what makes captured motion look grounded in AAA games, on by
default in `FellowMotionMatcher3D`:

* **Transitions worth their blend.** The playing pose continues unless a frame is cheaper by a bias, and the playing
  clip's frames near it are not candidates. Transitions fall from 4.9 to 2.4 a second.
* **Stride warping.** Each foot's place along the way is scaled about the hips by the character's speed over its
  clip's, and the hips are lowered to reach. The feet slide 0.062 m/s in contact (0.109), and half as much on slopes,
  stairs and uneven ground.
* **Lean.** The body turns over planted feet into the acceleration the character has and its clip lacks. Its lean in
  turns is 67 % of what balance asks (37 %), with an error of 2.8° (3.7°).

## 1. Checks (cloud)

* Matching Lab (16 characters, 60 s), each option alone and together; with them off the numbers are milestone 163's to
  the last digit (decision 0305's table).
* 64 characters with LOD: the same gains. The popping measure had counted a return from a lower LOD as one frame's
  jump; it no longer does (largest 906 → 126-133 mm, before and after the change alike).
* `ground_check.gd`: the feet's sliding halved on every ground. On an 8° slope the feet stand up to 1.6 cm off flat
  ground's (from 0.8), never in it.
* A character filmed before and after (`matching_m164_sbs.mp4`, `bench/matching_video.gd`).
* The showcase's 38 animation gates at their values; the module compiles for double precision.
* The laptop: `.tools\bundles\m164.bundle`. `Showcase - Dachy.bat` builds the engine anew as `.tools\bin\m164e` the next
  time it runs: the engine changed.

## 2. Next (animation, without the GPU)

* Orientation warping and strafing.
* Physical animation: the body driven by forces, hit reactions, ragdolls blended in.
* The showcase's player on motion matching instead of clips made by code.
* A richer database: styles, starts, stops and turns in place.
