# 0314 — Feet held through transitions: FellowFootLock3D on mocap rigs, after the inertializer

Status: **KEEP** the fixes; the combination measured, not a default · Branch `fellow/geometry` · Node
`FellowFootLock3D` (`modules/fellow_animation/fellow_foot_lock_3d.*`: the ground at rest, velocities through passes
with no time, `release_extension`) · Measured with `benchmarks/tools/animation_transitions/` (extended).

Checks: the cloud's CPU, headless, 128 CMU locomotion clips; no GPU.

## Problem

Decision 0313's inertializer leaves the feet sliding through a transition (the old pose's feet stay where the old
animation had them while the character moves on with the new one's root motion): 0.95 m/s over the frames the new
animation has a foot planted, the cross-fade 1.06. Decision 0295's `FellowFootLock3D` holds feet where they come
down. On the transitions course it held nothing: 2 foot-frames in 4 800. It finds each foot's sole points from the
rest pose, the ground there being the skeleton's origin — and a mocap rig like CMU's rests on its hips, its feet 0.9 m
below the origin: its "sole" was 0.9 m above the ankle and never came within the contact height.

## Change

* **The ground at rest**: the skeleton's origin, unless the feet's joints rest more than 0.1 m below it — then their
  lowest joint. Rigs standing on their origin (the showcase's) are unchanged.
* **Passes with no time**: a skeleton processing its modifiers twice in a frame (decision 0313) zeroed a held foot's
  offset velocity on the second pass; a foot let go then started back from standing still. The velocity is kept.
* **`release_extension`** (new, 1.0 by default: as before): a held foot is let go when holding it needs more than this
  share of the leg's length from the hip.
* **The course**: a game rig's root bone made from the mocap's hips (on the ground under them, turned with their
  heading; the root motion across the ground and about the vertical only, the hips' height and tilt left to them), the
  characters moved by it so that world-space modifiers see the world; each frame measured once from the pose drawn
  (decision 0313's correction); the legs' acceleration apart; characters with a foot lock after the cross-fade and
  after the inertializer.

## Measurements

The transitions course, 120 random switches between 128 CMU clips, 0.25 s blends (sliding over the frames the new
animation has a foot planted, within the transitions and everywhere; the 95th percentile of the largest angular
acceleration in a frame, of the legs and of every bone):

| | sliding in transitions | sliding everywhere | legs' acceleration p95 | all bones' p95 | feet held (foot-frames) |
|---|---:|---:|---:|---:|---:|
| cross-fade | 1.06 m/s | 0.369 | 13 844 deg/s² | 38 356 | — |
| cross-fade + foot lock | 0.85 | 0.343 | 43 982 | 54 348 | 4 023 |
| inertializer | 0.95 | 0.336 | 9 785 | 31 771 | — |
| **inertializer + foot lock** | **0.78** | **0.329** | 40 778 | 50 398 | 3 857 |
| … `release_extension` 0.98 | 0.89 | 0.379 | 16 211 | 34 124 | 1 753 |
| … 0.95 | 0.94 | 0.347 | 11 675 | 32 789 | 317 |

* The ground at rest: the lock now holds CMU's feet (3 857 foot-frames; before: 2).
* The inertializer and a foot lock after it: 26 % less sliding in transitions than the cross-fade, 17 % less than the
  inertializer alone.
* The price: the legs' acceleration's 95th percentile 4.2 times the inertializer's (the held foot's IK: near full
  length the knee swings with every millimetre the hip moves). Letting go at 98 % of the leg's length brings it to 1.7
  times, but the feet are held half as long and slide more everywhere than without a lock. The unlock radius (0.15-0.5
  m) and the release's half-life (0.06-0.15 s) left the bones' 95th percentile at 49 000-51 000.
* Keeping the velocity through passes with no time: the legs' 95th percentile 41 963 → 40 778 (the rest unchanged).

## Decision

KEEP the three fixes (the ground at rest, the velocity, `release_extension` at 1.0: the showcase's lock unchanged but
for the velocity fix). A foot lock after the inertializer is the least sliding measured, with jerkier legs; which to
take is the game's: both documented.

## Rejected / not done

* `release_extension` below 1 by default: less jerk but the feet held half as long, more sliding everywhere.
* Smoothing the held leg's IK (a limit on the knee's angular speed near full extension): not tried yet; the next step.
