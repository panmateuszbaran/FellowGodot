# 0316 — Foot placement: the feet on the ground under them (FellowFootPlacement3D)

Status: **KEEP**, a new node (nothing changes without it); in the rooftops showcase · Branch `fellow/geometry` · Node
`FellowFootPlacement3D` (`modules/fellow_animation/fellow_foot_placement_3d.*`, a `SkeletonModifier3D`), the leg IK
shared with `FellowFootLock3D` (`fellow_leg_ik.h`) · Measured with `benchmarks/tools/foot_placement/` (new); pictures by
`showcase/tools/foot_placement_lab.gd` (new).

Checks: the cloud's CPU, headless (the course, the showcase's 38 animation gates); pictures on lavapipe (the CPU's
Vulkan); no GPU.

## Problem

Locomotion clips are made on flat ground: the feet stand on the skeleton's origin. A character on a slope, a stair or
rubble stands where its capsule rests, and its feet go into the ground on one side and hang over it on the other. The
rooftops showcase walks on roofs of 20-30°: one foot in the tiles, the other in the air. AAA engines place the feet on
the ground under them (Unreal's Foot Placement and Leg IK, Unity's foot IK): rays under the feet, the legs bent onto
it, the pelvis lowered, the feet turned to the ground. FellowEngine had it only inside `FellowMotionMatcher3D`
(milestone 56); a character animated by an AnimationTree, an AnimationPlayer or code had nothing.

## Change

`FellowFootPlacement3D`, a skeleton modifier after the animation (and after a `FellowFootLock3D`: held feet placed
where they are held):

* **Rays** under each foot's heel and ball (the sole's points as `FellowFootLock3D` finds them), from `probe_up` (0.5 m)
  above the foot down to `probe_down` (0.6 m) below the character's ground, the character's own bodies excluded.
* **Each foot** raised or lowered so that every point of its sole is as high over the ground under it as the animation
  has it over the character's ground; turned to the ground while it is down (`foot_alignment`, at most 40°).
* **The hips** lowered as far as the lower foot goes down (eased), and at once as far as a leg needs to reach its foot
  (97 % of its length, or the animation's own stretch).
* **Eased in the world**, not relative to the body: a foot down stays where it is while the body climbs over it or is
  set down a stair (the body's own jolts do not reach the hips). **Followed with the goal's velocity** (a critically
  damped spring that tracks a moving goal): a swinging foot's ground rising along a slope is followed without lag;
  goals faster than 3 m/s (a step) are eased by the half-lives — up in 0.04 s, down in 0.1 s while the foot is down
  (in the air as fast as up). Velocities are measured when the pose changes, over the time since it last did (a
  skeleton may run its modifiers twice a frame, the time passing in one pass and the pose changing in the other).
* **A swinging foot looks ahead**: a fifth ray 0.05 s ahead of the ball (at most 0.15 m) — a step's edge is seen before
  the toe reaches it.
* Flat ground: nothing (offsets under 2 mm are none; no IK runs).

The leg IK (`fellow_two_bone_ik`, the steady knee pole of decision 0315, the springs) moved into `fellow_leg_ik.h`;
`FellowFootLock3D` uses it (its transitions course: every number the same).

## Measurements

The foot placement course (new): CMU's walk 35_08 (1.20 m/s, straight), looped, its root motion driving a
`CharacterBody3D` (capsule, `floor_step_height` 0.35 m) along lanes — flat, ramps up 10°, 20°, 30°, down 20°, stairs
up and down (0.16 by 0.30 m), a 20° cross slope, rubble (stones 3-12 cm) — each lane with the animation as it is and
with `FellowFootPlacement3D`. Once a frame from the pose drawn, rays under the soles: over the frames the animation has
a foot down (low and still), the sole's lowest point against the ground under it minus the animation's own clearance
(sunk below zero, floating above); a swinging foot more than 2 cm in the ground (a toe stubbing); the legs within 0.5 %
of straight; the legs' angular acceleration's 95th percentile.

| walk, 10 s | sunk p95 | floating p95 | worst | swinging foot in the ground | hips lowered (mean) |
|---|---:|---:|---:|---:|---:|
| ramp up 10° | 5.2 → **0.4** cm | 3.7 → **0.3** | 6.1 → 2.9 | 15.8 % → **0** | 3.3 cm |
| ramp up 20° | 10.9 → **0.8** | 8.6 → **0.8** | 12.2 → 6.2 | 27.7 % → **0** | 7.9 |
| ramp up 30° | 16.4 → **1.3** | 15.1 → **0.8** | 20.9 → 8.0 | 31.9 % → **0** | 14.4 |
| ramp down 20° | 6.0 → **1.2** | 10.0 → **0.1** | 12.8 → 1.8 | 11.3 % → **0.4 %** | 7.9 |
| stairs up | 5.1 → **0.2** | 15.8 → **4.3** | 31.6 → 15.4 | 13.1 % → **1.2 %** | 7.1 |
| stairs down | 2.0 → **0.0** | 16.1 → **2.5** | 29.9 → 11.7 | 3.8 % → **0** | 4.5 |
| cross slope 20° | 0.1 → 0.9 | 3.1 → **0.0** | 3.7 → 1.0 | 0 → 0 | 3.2 |
| rubble | 1.8 → **0.2** | 8.4 → **1.9** | 10.6 → 8.2 | 3.6 % → **0** | 4.2 |
| flat | 0 → 0 | 0.1 → 0.1 | 0.1 → 0.1 | 0 → 0 | 0 |

* Legs near straight: 0.2 % of the frames as animated; with it 0-0.2 % on every lane.
* The legs' acceleration's 95th percentile 14 900 deg/s² as animated, 15 000-21 200 with it (the knees bend and
  unbend with the ground; flat: the same 14 874).
* The CPU: 4-7 µs a character a pass (four rays, a fifth for a swinging foot; IK on both legs).
* The run (35_18, 3.1 m/s, 6 s): up 30° sunk 12.6 → 3.0 cm, floating 18.4 → 1.7; stairs up floating 16.2 → 2.2;
  rubble 8.4 → 3.7; stairs down floating 3.1 → **4.2** (worse: a running foot lands before the ground under it is
  followed down).
* After a `FellowFootLock3D` (the feet held across the ground first): on the 20° ramp floating 12.4 → 4.1 cm, sunk
  10.3 → 0.6, stairs up 16.0 → 5.7. (The lock alone with CMU's walk floats feet by 5 cm on flat ground — its own
  behaviour with this clip's low toes, `contact_height` 0.02 m.)
* The showcase's 38 animation gates (flat ground, the gaits, the climb, the roof's scramble): all pass, every value
  the same as milestone 173's.
* Pictures (`showcase/tools/foot_placement_lab.gd`, the rooftops' mannequin on lavapipe): across a 25° roof, the uphill
  foot in the tiles and the downhill one over them as made; both on the tiles with it, the uphill knee bent. Up stairs
  in mid-stride, the front foot inside the step as made; on it with it, the knee raised.

## How it got there (what was tried)

* **Eased relative to the body**: a foot down lagged behind the body climbing over it (floating at the end of every
  stance on the ramps). Eased in the world instead.
* **Eased without the goal's velocity**: a swinging foot's ground rises 1.1 m/s along a 20° ramp; the springs trailed
  it by 6-8 cm and the toe landed in the slope. The velocity was at first measured over the time since the goal last
  changed — always about zero (the goal changes slightly in the pass with the time too); measured over the time since
  the pose changed, it is right.
* **The hips eased only**: the leg left behind on the way up straightened (6-17 % of the frames on the ramps, 3 % on
  rubble). The reach is now enforced at once (the feet's offsets are eased: no jump).
* **Down slowly in the air too**: a foot stepping down a stair landed 7 cm above it at the 95th percentile; 2.5 cm
  now.
* **The look ahead**: none — a swinging toe in the stairs in 11 % of its frames; 0.03 s — 4.1 %; 0.05 s (kept) —
  1.2 %; 0.08 s — 0.4 %, but the feet landed further over the step before the edge and the ramps got worse (worst
  9.0 cm on 30°, the legs' acceleration +19 %).

## Decision

KEEP: a node for any skeleton, after the animation and after a foot lock; in the rooftops showcase (the player script
gives it the lock's weight: on the ground, not climbing, hanging or crawling).

## Rejected / not done

* Predicting where a swinging foot lands (from the animation's future or the root's trajectory): a modifier sees only
  the pose drawn; the look ahead does most of it on stairs. A running foot stepping down stairs is still late
  (above).
* The heel lifting instead of the hips dropping for the leg left behind on a steep slope (the hips go down 14 cm on
  30°): not tried.
* Uneven ground under the capsule (a body on a stone): the capsule rides over it and the feet reach down; a smaller
  capsule bottom or a body that follows the feet is the game's.
