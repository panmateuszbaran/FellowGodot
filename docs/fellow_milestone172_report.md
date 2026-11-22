# FellowEngine — milestone 172 report (feet held through transitions; decision 0313 corrected)

Status: **FINAL** (2026-11-22). Branch `fellow/geometry`. The cloud's CPU only, headless (no GPU, by the owner's
decision).

## 0. Result

**A correction first.** Milestone 171's transitions course measured each character at each of its skeleton's
updates, and a skeleton with modifiers updates twice a frame (the second time unchanged): the inertialized character's
frames were half measured as standing still. Measured once a frame, `FellowInertializer3D` against the cross-fade at
0.25 s: the feet slide 10 % less (0.95 against 1.06 m/s; 171 said 18 %), the bones' acceleration's median 41 % and
95th percentile 17 % lower, the turn in a frame's 95th percentile 6 % lower and its largest no better (50° against
44°; 171 said halved). The node stays — smoother, less sliding, one animation evaluated — and decision 0313, the
changelog, the options, the README and the class reference carry the corrected numbers.

**Feet held through transitions** (decision 0314). The inertializer leaves feet sliding (the old pose's feet stay
where the old animation had them while the character moves on). `FellowFootLock3D` (decision 0295) held nothing on
CMU's rig — 2 foot-frames in 4 800: it put the ground at the skeleton's origin, and a mocap rig resting on its hips has
its feet 0.9 m below it. Three fixes:

* the ground at rest is the feet's lowest joint when they rest more than 0.1 m below the origin (the showcase's rigs,
  standing on their origin, unchanged);
* a held foot's velocity kept through the skeleton's second modifier pass with no time (it was zeroed: a foot let go
  then started back from standing still);
* `release_extension` (new, 1.0: as before): let a held foot go before the leg straightens.

The transitions course now gives the mocap a game rig's root bone (made from the hips: root motion across the ground
and about the vertical), moves the characters by it, measures once a frame from the pose drawn and keeps the legs'
acceleration apart:

| 0.25 s blends | sliding in transitions | legs' acceleration p95 | feet held (foot-frames) |
|---|---:|---:|---:|
| cross-fade | 1.06 m/s | 13 844 deg/s² | — |
| cross-fade + foot lock | 0.85 | 43 982 | 4 023 |
| inertializer | 0.95 | 9 785 | — |
| **inertializer + foot lock** | **0.78** (−26 %) | 40 778 | 3 857 |
| … `release_extension` 0.98 | 0.89 | 16 211 | 1 753 |

The least sliding measured is the inertializer with a foot lock after it; its price is the held legs' jerk (4.2 times
the inertializer's acceleration p95: near full length the knee swings with every millimetre the hip moves). Both are
documented; the defaults are unchanged.

## 1. How it got there

* The lock holding nothing was found by counting held frames on the course: the sole points sat 0.9 m above the
  ankles. After the fix it holds 3 857 foot-frames.
* The second, unchanged skeleton update (the same one that broke 171's measurement) also zeroed the lock's offset
  velocity; keeping it lowered the legs' acceleration p95 from 41 963 to 40 778.
* The unlock radius (0.15-0.5 m) and the release's half-life (0.06-0.15 s) did not reduce the jerk (all bones' p95
  49 000-51 000); letting go before the leg straightens did, at the cost of held time.

## 2. Checks

* The transitions course (120 switches between 128 CMU clips) for six characters, `release_extension` 0.95/0.98/1.0,
  the unlock radius and half-life swept.
* `clang-format`, the class reference (`xmllint`, `make_rst.py --dry-run`: no warnings).

## 3. Next (without the GPU)

* Smoothing the held leg's IK near full extension (a limit on the knee's angular speed), to keep the lock's sliding
  without its jerk.
* Characters pushed by rigid bodies; the camera easing steps (`CharacterBody3D.get_last_step()`).
