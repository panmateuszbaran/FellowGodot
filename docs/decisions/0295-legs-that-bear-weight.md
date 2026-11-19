# 0295 — Legs that bear weight: the gaits built from the supporting leg, the feet held where they come down

Status: **KEEP** (engine: `FellowFootLock3D` is a new node, `FellowTraversalPose3D.soft_ik` is off by default; the
rest is the showcase's) · Branch `fellow/geometry` · Code: `modules/fellow_animation/fellow_foot_lock_3d.{h,cpp}`,
`modules/fellow_animation/fellow_traversal_pose_3d.{h,cpp}` (`soft_ik`, `_two_bone()`); the showcase's clips
(`tools/blender/mannequin_anims.py`: `gait_pose()`, `_hips_z()`, `_push_pitch()`, `foot_on_ground()`;
`tools/blender/mannequin_rig.py`: `soft_reach()`, `FPS`), its scenes (`tools/build_rooftops.gd`) and player
(`scripts/rooftops_player.gd`) · Checks: `showcase/tools/anim_quality.{gd,py}` (the gates of
`docs/showcase_animation_quality.md`), `bench/climb_check.gd`, the regression scenes, the demo timed, pictures.

## Problem

The owner, of milestone 154's video: "these over-straightened legs are supposed to be realistic?" Measured (the
bones after the modifiers, `anim_quality.gd`): walking, a knee within 5° of straight in 58 % of the frames (its median
3.6°: the IK's limit); running and sprinting, 31-32 %; mantling, 25 %. A walking knee is straight only at the heel's
strike and 30-40° bent as the toe leaves the ground; a running knee is never straight.

The cause, in the clips' generator: the stance ankle was moved over all the ground the body covers in a stance (1.04 m
walking): at the heel's strike 0.57 m ahead of the hips and 0.83 m under them — 1.0 m from the hip on a 0.86 m leg.
The IK could not reach: it locked the knee and left the foot short. The quality loop found five more faults on the
way, each measured before it was fixed:

* the ball's pivot was its joint, 2.4 cm over the sole: as the heel began to rise the foot sank (the feet 2.2 cm into
  the ground);
* the heel's and the ball's pivots were placed as if the foot were not turned out: changing from one to the other moved
  the foot 1.5 cm sideways;
* a swing began with the heel back on the ground (the ankle jumped down at the toe-off, the knee went straight) and
  ended still moving forwards with the body (the foot came down sliding);
* the clips were sampled at 30 Hz: a landing foot, drawn between keys a thirtieth of a second apart, slid millimetres a
  frame;
* a blend of two gaits mixes the same moment of their strides, not of their stances: a walk's foot down while a run's
  is in the air, the blend dragged the foot 7.7 cm in a stance speeding up to a run, 10 cm to a sprint, 21 cm stopping.

## Change

* **The gaits** (`gait_pose()`): the hips as high as the supporting legs reach at a knee's flexion over the stance (a
  curve per gait from gait analysis), never higher than every planted foot can be reached (no target asked of the IK is
  out of its reach); in the push the heel raised as far as the knee's flexion needs; the stride's place and the cadence
  from walking and running data (walk 0.95 s, its stance 58 %, 44 % of it ahead of the hips); a swing that leaves and
  reaches the ground at the ground's speed (a Hermite curve; running, with part of it), the foot kept pointing down
  while it lifts; the pivots the turned foot's own, the ball's on the sole; a jog (3 m/s) between the walk and the run;
  the clips at 60 Hz (made and imported).
* **Soft IK**: in the generator, past 99 % of a limb's reach its target is eased towards 99.7 % of it (a knee never
  less than about 9° bent); in `FellowTraversalPose3D`, `soft_ik` (off; the showcase: 0.97).
* **`FellowFootLock3D`** (engine, a new node): each foot held where its sole comes down (heel or ball, handed over
  between them), only its slide taken out (the roll is the animation's), by two-bone IK; let go when lifted or pulled
  0.3 m; eased back over `blend_halflife`. The showcase gives it its weight on the ground (eased over 0.1 s) and resets
  it at the demo's cuts.
* **The showcase's player**: the gait's blend and the idle's eased (0.08 s, 0.15 s; a stop from a sprint had taken the
  pose to idle's in a tick).

## Checks

* **`anim_quality.py`** (the bones after the modifiers at 120 Hz; milestone 154's build against this one):

  | gate | band | milestone 154 | now |
  |---|---|---|---|
  | walk: knee straight (share of frames) | ≤ 12 % | 57 % | 0 % |
  | walk: knee as the toe leaves | 30-45° | 3.6° | 32.5° |
  | walk: knee at its most bent in the swing | 55-70° | 58.7° | 69.0° |
  | walk: knee after the strike | 5-20° | 4.6° | 20.6° |
  | walk: hip at the strike | 25-35° | 22.6° | 34.6° |
  | walk: ankle as the toe leaves (plantarflexion) | 15-30° | -0.3° | 18.5° |
  | walk: ankle after the strike | 10-25° | 21.5° | 8.9° |
  | walk: skate (cm a planted foot moves in a stance) | ≤ 2 | 2.26 | 0.006 |
  | walk: feet in the ground | ≥ -2 cm | -2.16 | -0.77 |
  | run: knee's least flexion | ≥ 15° | 3.6° | 14.7° |
  | run: knee straight | 0 % | 31 % | 0 % |
  | run: knee at contact / in stance / in the swing | 15-45 / 40-65 / 90-125° | 9.3 / 49.8 / 110 | 23.2 / 42.1 / 124 |
  | run: skate | ≤ 2 cm | 2.55 | 2.33 |
  | sprint: knee's least / straight / swing | ≥ 15° / 0 % / 110-130° | 3.6 / 35 % / 124 | 12.8 / 0 % / 129 |
  | sprint: skate | ≤ 2 cm | 4.20 | 3.14 |
  | walk to run / run to sprint / stop: worst stance's skate | ≤ 5 cm | 7.66 / 10.1 / 20.6 | 2.24 / 2.29 / 1.66 |
  | changes of gait: the joints' fastest against the gaits' | ≤ 1.5 | 1.27 / 1.12 / 1.40 | 1.71 / 1.43 / 1.27 |
  | hanging / hold to hold / mantle: knee straight | ≤ 1 % | 0 / 0 / 25 % | 0 / 0 / 0 % |

  25 of the 32 gates pass (milestone 154: 14). Not yet: the walking knee after the strike (20.6°) and the ankle after it
  (8.9°: it was 21.5° — further from its band), the running and sprinting knees' least flexion (14.7°, 12.8°, at the end
  of the push), the running and sprinting skate (2.3, 3.1 cm: the foot comes down still moving forwards — a swing that
  reached the ground at the ground's speed straightened the knee), the walk-to-run change's fastest joint (1.71 times
  the gaits' fastest; 1.04-1.71 over the last iterations, as small changes move the moment the jog's and the run's
  strides cross). The next iterations of the loop.
* **`climb_check.gd`**: the 37 lanes' events, places and times value for value on GodotPhysics3D and Jolt;
  `last_check` (the last ledge check's diagnosis) of two or three free-climb lanes swapped among them — with the new
  class registered and not with it unregistered (the same build otherwise): registering a class moves object ids, and
  the free climb's last check depends on their order; no lane's outcome changes.
* **Regression suite**: `climb`, `ledge_corners`, `traversal` exact.
* **The demo**: every shot in its time (its walk along the beam to the viewpoint now within 0.2 m of each point of its
  path: 0.35 m had left it 0.2 m off the beam's line, where the edge guard of decision 0294 stopped it).
* **Pictures** (lavapipe): the walk and the run from beside — the knees bent under the weight, the heel striking, the
  foot rolling onto the ball, the swing's knee high.

## Decision

KEEP. The loop goes on (`docs/showcase_animation_quality.md`).

## Rejected / not done

* **A time warp of the gaits' clips** (every gait's stance at the same share of its cycle, so that a blend mixes like
  stances): the clips are played at an even rate; a warped stance moves the foot at another speed than the ground's.
* **Holding the ankle** (the first `FellowFootLock3D`): the ankle moves across the ground while the heel rolls and the
  ball pivots; held, the walk's knee as the toe left was 17° (without the lock 35°). The sole's point under the heel or
  the ball is held (26-33°, with the gaits' other changes).
* **A soft limit from 97 % of the reach in the foot lock**: it bent the legs the clips stretch near full length (the
  walk's ankle after the strike 8.2° → 5.2°); the lock's IK reaches what the animation reaches (its soft limit from
  99.8 %).
* **Grabbing a foot within 3 cm of the ground**: a landing foot was held short of where it came down (the walk's knee as
  the toe left 26° → 33° at 1.2 cm, the showcase's).
* **Feet on slopes** (the roofs' tiles; `FellowMotionMatcher3D` has ground adaptation, the showcase's tree does not):
  next.
