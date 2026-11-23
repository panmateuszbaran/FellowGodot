# 0328 — A step before the fall, caught bodies put back: the stepping strategy before the legs give way, the feet landing flat, a body caught near its place put there

Status: **KEEP**, on by default — `FellowPhysicalAnimation3D.fall_step_time` (0.4 s; 0: milestone 185's fall where it
stands), `FellowPhysicalAnimation3D.stuck_rescue` (on) · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_physical_animation_3d.{h,cpp}`: `apply_hit()` (the balance's branch), `_update_balance()`
  (the staggering state), `_set_state()`, `_fall()` (the feet), `_fall_leg()`, `_fall_land()`, `_fall_foot_target()` (new),
  `_fall_pose()`, `_sole_shapes()` (a sole's up), `_drive()` (the stuck rescue), `_place_free()` (new);
  `get_balance_stats()` adds `fall_steps`, `get_stats()` adds `stuck_rescues`;
* `benchmarks/project/bench/balance_check.gd` (a fall's step, a foot caught under a thin floor, `--fall_step`,
  `--stuck_rescue`).

Checks: `balance_check.gd` and `physical_check.gd` on Jolt Physics (headless), the character filmed on lavapipe. No GPU
(the owner's wish).

## Problem

Milestone 184's and 185's open items:

* **No step before a fall.** A push it could not catch (its capture point beyond `fall_threshold`, a step's reach) made
  the legs give way where it stood, 0.02 s after the push: the feet never left the ground (`balance_check.gd`: 0 lifts
  in five falls). People pushed that hard step first — the stepping strategy, the first response to a push beyond what
  the ankles and hips can take (Maki and McIlroy 1997, Phys. Ther. 77(5)) — and fall when the step was not enough. A
  puppet whose strings were cut.
* **A body caught near its place stayed caught.** Only a body 0.75 m from where the animation has it was put back
  (milestone 166's rescue). A foot that went through a thin floor (a roof's slab, a step's) was held under it by the
  drive pulling it straight up: `balance_check.gd`'s new trial, a foot's body 8 cm under a 3 cm slab, standing — 54 mm
  from its place after 0.25 s, 62 after 0.5, 74 after a second, and on.

## Change

* **A step first** (`fall_step_time`, 0.4 s): a hit whose capture point is beyond `fall_threshold` but within twice it
  (a push one step could almost catch) first pushes the motion matcher that way as fast as the pendulum would carry the
  centre of mass to the capture point (ω c, at most 2.5 m/s, decaying over twice the step's time): the database's own
  steps, a stagger (`staggered` is emitted; the hit's local share kicks the struck part as a stagger's does). After
  `fall_step_time` the fall begins from the pose the step reached, as fast as milestone 184's would (ω (c −
  `fall_threshold`)); further than twice the threshold, with 0 or without a motion matcher it falls where it stands.
* **The feet land flat**: as the legs give way, each foot (the motion matcher's: mid-step, on its toes or its heel) turns
  flat about its ankle (its sole's up to the world's) and comes down to where the lower one stands, over 0.15 s, eased;
  the knee's IK goes to the ankle's new place. A foot in the air stayed in the air through the fall.
* **A caught body put back** (`stuck_rescue`, on): a body held at full strength more than 5 cm from its place without
  coming nearer (its velocity towards it under 0.1 m/s) for 0.3 s is put there if the place is free — its shapes, 2 cm
  higher, meet nothing it collides with but the character's own bodies (a shape query) — while the animation is
  followed (with balance: standing or staggering; a fall's and a get-up's targets may be in the ground on purpose: with
  the rescue on in a fall the knees were put back and the joints tore 91 mm open).

## Measurements

`balance_check.gd` (Jolt, 60 Hz), milestone 185 (`--fall_step=0 --stuck_rescue=0`) → 186; falls are 200 N·s at the
chest (the capture point 0.86 m: within twice `fall_threshold`):

| | milestone 185 | milestone 186 |
|---|---:|---:|
| the fall begins after the push | 0.02 s | **0.40 s** |
| a foot lifted before it (steps) | 0 | **1** (the stepping foot 0.49-0.65 m) |
| the hips at 0.3 m after the push | 0.60-0.73 s | 1.02-1.23 s (0.62-0.83 s after the fall begins) |
| coming down at | 0.71-2.50 m/s | 0.22-2.47 m/s |
| lands (pushed from the front, behind, left, right) | back, side, side, side | side, side, face, face |
| the feet's slip (all, one stance's most) | 4.1-18.8 / 2.7-9.7 cm | 5.1-20.8 / 1.6-17.2 cm |
| the joints' largest gap | 16-27 mm | 17-31 mm |
| a limp knee's flexion; an elbow's | 114-155°; 17-156° | 80-159°; −5-152° |
| the trunk's bend at most | 42-56° | 64-94° |

* The stepping foot lands on the ground (its body 0.068-0.078 m up, standing 0.067) and stays there as the legs give
  way; before the feet were flattened it stayed 4-5 cm up.
* Staggers (20-160 N·s): as far as before, the joints' gaps the same, the feet's slip within 7 mm of before.
* With `fall_step_time` 0 (the fall where it stands, now with the feet turned flat): the hips at 0.3 m after
  0.60-0.73 s at 0.70-2.49 m/s, the joints' gaps 16-27 mm, the trunk's bend 39-56° — milestone 185's.
* With balance off, `physical_check.gd`: milestone 185's numbers (the bodies 3.4 mm from the walk, the chest and the
  head knocked 58 and 134 mm, the ragdoll 30 mm into the floor). The drive 11.7 µs a step (12.8).
* A foot caught 8 cm under a 3 cm slab, standing: 54 / 62 / 74 mm from its place after 0.25 / 0.5 / 1 s →
  **54 / 0.5 / 0.4 mm** (put back once).

## How it got there

* The first stuck rescue ran whenever the drive was at full strength: in a fall the bodies are held to the buckling
  pose, the knees' targets near the ground, and a knee put back tore its joints 91 mm open (22 without). It runs only
  while the animation is followed.
* With the step the foot was still in the air when the legs gave way (7 cm up) and the fall held it there. Lowered by
  its target's sole it came down only 3 cm: the foot, pitched on its toes, had its sole's height measured flat. Turned
  flat about the ankle first, it lands.
* A foot's body put 8 cm into the 1 m thick test floor was pushed out by Jolt within a quarter of a second: a reset
  cannot leave it caught there. A thin floor can.

## Decision

KEEP. A push it cannot catch is stepped after first, the database's own step, and the legs give way from where the step
took it, the feet landing flat; a body caught under a floor near its place is put back in a third of a second.

## Rejected / not done

* More than one step (stepping until the capture point is caught, then falling only if it is not): the push's speed
  carries one step in 0.4 s; a second would need the capture point followed through the step.
* The stepping foot chosen by the side pushed (the database's step is the one the search finds).
* The trunk bends further in these falls (64-94°, within the spine's and the neck's limits): the ragdoll lands from a
  wider stance.
