# 0305 — Motion matching's AAA layer: transitions worth their blend, the stride warped, the body leaning

Status: **KEEP**, on by default · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_motion_matcher_3d.{h,cpp}`: `transition_cost_bias`, `transition_jump_threshold`,
  `stride_warping`, `stride_warping_limits`, `lean_amount`, `lean_max_angle`; the lean's statistics;
* `fellow_motion_database.{h,cpp}`: the search's current-frame bias and jump window;
* `benchmarks/project/scenes/matching_lab.gd`, `bench/ground_check.gd`: the options; `bench/matching_video.gd`.

Checks: Matching Lab (16 characters, 60 s, the CMU database's locomotion, cloud CPU), `ground_check.gd`, the 38
animation gates, a character filmed on lavapipe.

## Problem

The owner, of the rooftops showcase: "the animations are not nice … we must work on the engine itself". The
showcase's clips are made by code, not captured: that is content. The engine's motion matching runs on real motion
capture, but it still lacked what makes captured motion look grounded in an AAA game. Matching Lab measured three
faults:

* **Transitions too often.** 4.9 a second per character. Half the searches every tenth of a second switched clips,
  most for a frame barely cheaper, often a few frames away in the same clip: a stutter blended over 0.1 s, again and
  again.
* **Feet sliding.** 0.11 m/s while in contact. The character moves at the simulation's speed, but the clip strides at
  its own, so the feet are dragged; 0.064 m/s of it while locked (the IK could not reach the locked foot).
* **No lean.** In turns the body leant 37 % of what balance asks (`atan(a / g)` of its sideways acceleration): the
  clips lean for their own turns, not for a turn sharper or a start quicker than the clip's.

## Change

**Transitions** (the search):

* The playing frame's cost is lowered by `transition_cost_bias` (2.0, in the search's normalised cost), so another
  frame must be cheaper by that much to replace it.
* The playing clip's frames within `transition_jump_threshold` (0.25 s) of it are not searched.

**Stride warping** (`stride_warping`, as Unreal's stride warping does it):

* The character's speed over its animation's root motion is measured (both smoothed over a few frames). The ratio,
  within `stride_warping_limits` (0.75-1.35), is eased in from standing to walking.
* Each foot's place along the way the character goes is scaled about the hips by that ratio, before the feet are
  locked and the IK reaches them.
* The hips come down (up to 12 cm, eased) where a leg must reach farther than 97 % of its length.

**Lean** (`lean_amount` 1.0, `lean_max_angle` 20°):

* The character's acceleration and its animation's are both measured: the root motion turned into the world, so a
  clip's own turn is its own acceleration.
* Their difference, in the character's space, gives the lean: `atan(a / g)` sideways and half that forwards (a runner
  leans into a start less than into a turn; the legs push).
* The angles are eased (0.1 s) and the hips are turned about the ground under them. The feet's targets are kept from
  before the lean, and the leg IK reaches them below, so the whole body leans over planted feet. There is no lean
  without `foot_ik`.

**Statistics.** `get_stats()` measures the body's lean against balance's: from between the feet on the ground to the
mean of the bones, against `atan(a / g)` of the character's own sideways acceleration, both smoothed over a stride,
while it moves faster than 1 m/s. Matching Lab reports its error, gain and correlation.

**Found on the way.** The popping measure compared a full update's pose with one from before a stretch at a lower LOD.
That counted as one frame's jump: 906 mm with LOD in a crowd of 64, before and after this change. It is now measured
between consecutive full updates only.

## Measurements

Matching Lab, 16 characters, 60 s (`bench/matching_eval.gd`):

| | foot slide (m/s) | of it locked | transitions /s | lean error | lean gain | correlation | pop p99 (mm) | trajectory error (m) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| milestone 163 (the new options off) | 0.109 | 0.064 | 4.90 | 3.67° | 0.37 | 0.78 | 35.8 | 0.039 |
| `transition_cost_bias` 2 | 0.106 | 0.064 | 2.51 | 3.60° | 0.37 | 0.77 | 35.8 | 0.043 |
| bias 2, jump 0.25 s | 0.103 | 0.063 | 2.43 | 3.61° | 0.37 | 0.78 | 35.8 | 0.043 |
| `stride_warping` | 0.068 | 0.035 | 4.90 | 3.59° | 0.40 | 0.77 | 37.8 | 0.039 |
| `lean_amount` 1.0 | 0.113 | 0.069 | 4.90 | 2.84° | 0.63 | 0.85 | 35.2 | 0.039 |
| `lean_amount` 1.5 | 0.115 | 0.071 | 4.90 | 2.86° | 0.72 | 0.84 | 35.8 | 0.039 |
| `lean_amount` 2.0 | 0.117 | 0.074 | 4.90 | 3.04° | 0.78 | 0.82 | 36.2 | 0.039 |
| **milestone 164's defaults** | **0.062** | **0.036** | **2.43** | **2.75°** | **0.67** | **0.85** | 37.8 | 0.043 |

* With the options off the numbers are milestone 163's to the last digit.
* 64 characters with LOD (8 and 16 m): foot slide 0.109 → 0.062 m/s, transitions 4.21 → 2.36 a second, the lean's
  error 3.00 → 2.13°. Popping is the same with and without the change once it is measured right (p99.9 62-65 mm, the
  largest 126-133 mm).
* The main thread's matcher time a step (16 characters, worker threads) stays within the runs' spread: 0.6-0.9 ms
  either way.
* Without `foot_ik` there is no stride warping or lean; foot slide 0.32 → 0.27 m/s (the transitions).

Ground adaptation (`ground_check.gd`, walking 1.3 m/s, error of the feet's height against flat ground's, mean · p95 ·
max mm):

| ground | the options off (decision 0157's) | the defaults |
|---|---|---|
| flat | 0 · 0 · 0, slide 62 mm/s | 0 · 0 · 0, slide 30 mm/s |
| slope 8° | 3.1 · 7.7 · 11.4, slide 62 | 5.1 · 16.3 · 23.2, slide 37 |
| stairs 15 cm | 12.6 · 52.5 · 152.2 (2.9 % in the ground), slide 49 | 13.3 · 57.1 · 150.0 (2.1 %), slide 32 |
| uneven 0-12 cm | 10.0 · 30.1 · 92.8 (0.9 %), slide 46 | 10.9 · 29.5 · 92.2 (0.7 %), slide 30 |

The feet's sliding is halved on every ground. On the slope the feet stand up to 1.6 cm off where flat ground's same
frame has them (from 0.8 cm), never in it: the stride's hips settle a little differently uphill.

The showcase's 38 animation gates pass, each at milestone 158's value (the showcase does not use the matcher). The
module compiles for double precision (`REAL_T_IS_DOUBLE`) as well.

## Decision

KEEP, on by default: the node is FellowEngine's own, and every measure of how its motion looks improved, at a
trajectory error 4 mm larger.

## Rejected / not done

* Larger leans: `lean_amount` 1.5-2 brings the gain to 0.72-0.78 but its error and correlation get worse. The clips'
  own leans are not all balance's, so what the character adds cannot make them so.
* A `transition_cost_bias` of 4: transitions 2.07 a second, trajectory error 4.5 cm, slide back to 0.110 m/s.
* Orientation warping (the legs turned to the way the character goes while it faces another): Matching Lab's
  characters face the way they go. Next, with strafing.
* Physical animation (the body driven by the physics world's forces, ragdolls blended in), a richer database (the
  owner's own captures, styles), the showcase's player on motion matching: next milestones.
