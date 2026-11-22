# FellowEngine — milestone 173 report (the foot lock lets go without jumps)

Status: **FINAL** (2026-11-22). Branch `fellow/geometry`. The cloud's CPU only, headless (no GPU, by the owner's
decision).

## 0. Result

Milestone 172 left the foot lock after the inertializer with jerky legs (the 95th percentile of their angular
acceleration 4.2 times the inertializer's alone) and proposed limiting the knee's speed near full extension. Looking
at the worst frames one by one showed three other faults in `FellowFootLock3D` (decision 0315):

* **A foot let go jumped.** A held point out of the leg's reach is shown where the leg reaches; letting go, the lock
  started back from the point itself and pulled it within reach towards the animated foot — up to 17 cm in a frame
  when the held foot was off to the side. Planted feet moved at up to 17 m/s. It is now let go from where it is shown,
  and brought within reach straight towards the hip where that is the shorter way.
* **The knee swung round the leg.** Its pole came from the animated knee, which lies on the hip-foot line when the
  animated leg is straight: against a held foot elsewhere its side flipped (a bone turned 85° in a frame). Where the
  leg is straight the knee now bends towards the foot's forward.
* **The held velocity was zero** when a script advances the AnimationPlayer (the time and the pose change in
  different modifier passes); it is measured over the time since the pose changed.

| inertializer + foot lock, 0.25 s blends | milestone 172 | milestone 173 |
|---|---:|---:|
| sliding in transitions | 0.785 m/s | 0.785 |
| sliding everywhere | 0.329 m/s | **0.309** |
| held feet outside transitions | 0.164 m/s | **0.079** (the animation's own 0.078) |
| legs' acceleration p95 | 40 778 deg/s² | **38 268** |
| every bone's p95 | 50 398 | **45 073** |
| the largest turn in a frame | 84.8° | **52.1°** |
| a planted foot's fastest | 17.2 m/s | **5.1 m/s** |

The showcase's 38 animation gates (`showcase/tools/anim_quality.py`) all pass against the milestone 172 build with no
regression (the walk-to-run pop 0.90 → 0.84, the approach's fastest joint 2 090 → 1 930 °/s).

The legs remain 3.9 times jerkier than the inertializer's alone; what is left is mostly the moments a foot is held or
let go (its path stopping and starting), not the knee.

## 1. How it got there

* A diagnostic copy of the course (not committed) logged, frame by frame, the legs' acceleration, each foot's lock
  state, the leg's extension and the held offset. 63 of the top 103 frames were a foot held or let go; the planted
  feet's speeds showed teleports of 10-17 m/s while the IK showed the leg at 99.9 % and then 99 % of its length.
* Five other ways were measured and rejected (decision 0315): dragging held feet along past a share of the leg (less
  jerk, but the foot slides with the body: worse than no lock), easing the reach in over 0.2 s after letting go (the
  legs −25 %, but the showcase's sprint got a knee within 6° of straight and the walk's toe-off knee left its band),
  not holding a foot again until it lifts, slowing a foot coming down, a quintic return after letting go.
* The release rule's first version (from where shown and straight towards the hip, always) made the showcase's walk
  straighter at toe-off (28.6°; gate 30-45°): kept only where the old way jumped (more than 2 cm longer, the leg not
  reaching the held point).

## 2. Checks

* The transitions course (120 switches, 128 CMU clips; the cross-fade with a lock too), ablations of the knee's pole
  and the velocity.
* `showcase/tools/anim_quality.py run --baseline` (milestone 172's build): 38/38, exit 0.
* `clang-format`, the class reference (`xmllint`, `make_rst.py --dry-run`).

## 3. Next (without the GPU)

* The lock's remaining jerk at holding and letting go (where a foot stops and starts), against a slide it must not
  bring back.
* Characters pushed by rigid bodies; the camera easing steps (`CharacterBody3D.get_last_step()`).
