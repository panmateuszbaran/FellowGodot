# 0315 — The foot lock lets go without jumps; a steady knee

Status: **KEEP** (a change to `FellowFootLock3D`'s behaviour, measured on the transitions course and the showcase's 38
animation gates) · Branch `fellow/geometry` · Node `FellowFootLock3D` (`modules/fellow_animation/fellow_foot_lock_3d.*`)
· Measured with `benchmarks/tools/animation_transitions/` and `showcase/tools/anim_quality.py`.

Checks: the cloud's CPU, headless, 128 CMU locomotion clips; the showcase's gaits and climb; no GPU.

## Problem

Decision 0314 left the foot lock after the inertializer jerky: the held legs' angular acceleration's 95th percentile
4.2 times the inertializer's alone, and proposed limiting the knee's speed near full extension. A diagnosis of the
course frame by frame (the frames in the top 5 % of the legs' acceleration) found something else:

* **A foot let go jumped.** 63 of those 103 frames were a foot being held or let go. A held point out of the leg's
  reach is shown where the leg reaches (the IK stops short of it, higher and nearer); letting go, the lock started back
  from the point itself, and decision 0296's rule — never eased back from further than 99 % of the leg's reach —
  pulled it within reach *towards the animated foot*. With the held foot off to the side of the leg that is a long
  way: up to 17 cm in a frame for a 7 mm change of the leg's length. Planted feet moved up to 17 m/s; 14 frames faster
  than 6 m/s. Outside the transitions, feet held by the lock slid more than the animation's own (0.164 against
  0.078 m/s on the frames the reference has them planted), two thirds of it in 7 such jumps — a foot let go, jumped,
  and held again in the skeleton's second pass of the frame.
* **The knee swung round the leg.** The knee bends towards a pole made from the animated knee. Where the animated leg
  is nearly straight, its knee lies on the line from the hip to the animated foot, and against a held foot elsewhere
  its side of the line flips: the largest bone turn in a frame was 85°.
* **The held velocity was zero** on the course: the skeleton processes its modifiers twice a frame there (an
  AnimationPlayer advanced by a script), the time passing in one pass and the pose changing in the other; measured in
  the pass with the time, the held offset's velocity was always zero (decision 0314 kept it through the pass with no
  time: while held it was zero already).

## Change

* **Let go from where the foot is shown** when the leg could not reach the held point (shown more than 5 mm from it):
  across the ground from where it was shown, its height the animation's plus what the leg's reach lifted it by (a
  height then eased out over `blend_halflife`, also if the foot is held again meanwhile).
* **Within reach the short way**: towards the animated foot as before (decision 0296), unless that moves it more than
  2 cm further than straight towards the hip — then straight towards the hip, its velocity away from the hip dropped.
* **A steady knee**: the pole from the animated knee's offset from the hip-foot line where the leg is bent (more than
  5 % of the leg's length), from the foot's forward where it is straight, blended between.
* **The held velocity** measured over the time since the pose last changed (zeroed after 0.1 s without a change).

## Measurements

The transitions course (decision 0314's), 120 random switches between 128 CMU clips, 0.25 s blends, the inertializer
with a foot lock after it:

| | sliding in transitions | sliding everywhere | legs' acceleration p95 | all bones' p95 | largest turn in a frame | planted foot's fastest |
|---|---:|---:|---:|---:|---:|---:|
| inertializer alone | 0.95 m/s | 0.336 | 9 785 deg/s² | 31 771 | 50.3° | — |
| + foot lock, milestone 172 | 0.785 | 0.329 | 40 778 | 50 398 | 84.8° | 17.2 m/s (14 frames > 6) |
| **+ foot lock, milestone 173** | **0.785** | **0.309** | **38 268** | **45 073** | **52.1°** | **5.1 m/s (none > 6)** |
| … without the steady knee | 0.785 | 0.309 | 40 210 | 48 687 | 84.9° | |
| … without the velocity | 0.782 | 0.308 | 38 268 | 45 120 | 52.1° | |
| cross-fade + foot lock, 172 → 173 | 0.855 → 0.850 | 0.343 → 0.341 | 43 982 → 41 508 | 54 348 → 51 394 | 83.6° → 43.6° | |

* Outside the transitions the held feet now slide as little as the animation's (0.079 m/s against 0.078; before
  0.164), 2.03 m in all down to 0.97.
* The steady knee: the largest turn in a frame from 85° to 52° (the inertializer's own 50°), the legs' acceleration
  −5 %, every bone's −7 %.
* The velocity: no change on the course (a correctness fix for players advanced by scripts).
* The showcase (`anim_quality.py` against the milestone 172 build): 38 of 38 gates, no regression; the walk-to-run pop
  0.90 → 0.84, the approach's fastest joint 2 090 → 1 930 °/s, the run's skate 1.57 → 1.51 cm, the rest equal.

The legs stay jerkier than the inertializer's alone (3.9 times its 95th percentile): what remains is mostly the
frames a foot is held or let go and the one after (77 % of the top 5 %) — the foot's own path stopping and starting —
and a held leg near full length (13 %).

## Decision

KEEP: no jumps, a steady knee, the same sliding in transitions, less everywhere, every showcase gate as it was.

## Rejected / not done

* **Dragging a held foot along** past a share of the leg's length (`drag_extension`, eased or hard, towards the
  animated foot or the hip): the legs' acceleration's 95th percentile 15 000-19 000, but the dragged foot slides with
  the body — 0.84-0.93 m/s in transitions, 0.36-0.40 everywhere (worse than no lock). CMU's stance legs reach 98 % of
  their length (median), so any limit below that drags every stance.
* **Easing the reach in over 0.2 s after letting go** (no 7 mm in a frame near full length): the legs' 95th percentile
  28 600, but the showcase's sprint then has a knee within 6° of straight (gate: 15°) and the walk's knee at toe-off
  28.6° (gate: 30-45°). Letting go from where shown and straight towards the hip *always* (not only where the old way
  jumped): the walk's toe-off knee 28.6-29.8°.
* **Not holding again a foot let go while still down** until it lifts or is back within 3 cm: the legs −9 to −17 %,
  sliding in transitions +6 %.
* **A foot coming down slowed to a stop** (its lock point carried on, its velocity halving every 0.02-0.06 s): sliding
  in transitions 0.90-0.94, the legs about the same.
* **A quintic return after letting go** (Bollo's inertialization, 0.15-0.35 s, no step in the acceleration): the legs
  unchanged (38 000), sliding everywhere 0.33-0.37.
* **Limiting the knee's angular speed** (decision 0314's proposal): not tried — the diagnosis put the jerk in the
  jumps, the pole and the foot's path, not in the knee near full extension.
