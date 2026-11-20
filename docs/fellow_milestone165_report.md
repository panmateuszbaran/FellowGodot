# FellowEngine — milestone 165 report (motion matching's orientation warping)

Status: **FINAL** (2026-11-20). Branch `fellow/geometry`. Everything ran in the cloud (CPU; the film on lavapipe); the
laptop's GPU stays paused.

## 0. Result

The owner: "go on to AAA". The next piece of the animation's AAA layer (decision 0306): `FellowMotionMatcher3D`'s
orientation warping, on by default.

A third-person game's character often faces one way and walks another, and the clip motion matching finds goes some
degrees off the asked way. Now the hips turn to the asked way, the spine turns back over its bones so the chest keeps
the facing, and the root motion turns with them.

Matching Lab:

* walkers strafing round the square's centre: the feet's sliding 0.158 → 0.127 m/s, the trajectory error
  9.1 → 7.7 cm;
* walking as before: the trajectory error 4.3 → 3.8 cm, the lean in turns 67 → 71 % of balance's, the sliding as
  before.

## 1. Checks (cloud)

* Matching Lab, 16 characters, 60 s, walking and strafing (`--matching_strafe=1`), the largest angle at 45°, 90° and
  135° (decision 0306's table).
* A strafing walker filmed with and without the warping (`strafe_m165_sbs.mp4`).
* The class reference validates.
* The laptop: `.tools\bundles\m165.bundle`; `Showcase - Dachy.bat` builds the engine as `.tools\bin\m165e`.

## 2. Next

* Physical animation: the body's bones as physics bodies driven to the animated pose, knocked by hits and recovering,
  ragdolls blended in. Measured without the GPU: the pose's error with no hit, the recovery's time, stability, the
  cost.
* The showcase's player on motion matching.
* A locomotion database with the eight directions at each pace (CMU's has few side steps and none at a jog).
