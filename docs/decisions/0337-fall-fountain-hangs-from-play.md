# 0337 — From the owner's play: the fall, the step up onto the fountain, the hangs and climbs

Status: **KEEP** · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_traversal_3d.cpp`: a mantle with a clip's root motion — its way along the ground
  held off the wall until the feet are within `MANTLE_CROSS` of the top, the hold eased in and let go at no more than
  `MANTLE_CATCH_UP` over the clip's speed (in place of decision 0296's `MANTLE_RELEASE`); a braced foot
  (`_hang_foot()`) out as far as the stone that stands out above it, its ankle 0.15 m off the stone (0.12); the feet of a
  move between holds on the stone found, as the hang's (they had been on the plane of the face at the capsule's side);
* `showcase/scripts/rooftops_camera.gd`: the speed the framing follows smoothed and held through a mantle; the hang's
  and climb's framing eased in and out, not a mantle's;
* `showcase/scripts/rooftops_player.gd`: `MOCAP_KEEP_OWN` — the fall, the hangs and the climb the mannequin's own;
* the tools: `showcase/tools/camera_look.gd` (new: the camera's jerk and the body's way over a step up, headless),
  `showcase/tools/anim_quality.gd` and `.py` (limbs through the buildings, per segment).

## Problem

The owner, playing the rooftops after milestone 194:
1. "the character falls face down and then magically turns onto its feet, even in an ordinary jump";
2. "stepping up onto the fountain, the camera jumps";
3. "hanging from a ledge and not moving, the character makes very strange movements";
4. "climbing, parts of the character's body go through the building";
5. "the climbing animations are not smooth".

## What was found

1. **The fall** was Mixamo's "Falling Idle" (decision 0335): a skydiver's pose, face down, arms and legs spread. After
   a jump's top the player plays the fall, and on landing the landing clip stands the body up — from face down.
2. **The fountain** (headless, `camera_look.gd`: the player runs at the rim, 0.75 m, from 3.5 m and 6 m away):
   * the step up is a low mantle with the mannequin's clip (`mantle_low`: 1 m up, 0.55 m forward). Decision 0296 held
     the body off the wall until the clip's root was over the top — on a ledge lower than the clip's, its rise scaled
     down reaches the top only at its end — then let it go over 18 % of the mantle: the body crossed the rim's 0.7 m in
     the last tenth of a second, at **9.7 m/s**;
   * the camera switched to the hang's framing (half way to level, 0.5 m closer) in one frame as the mantle began, and
     back as it ended: **0.77 m and 11° in a frame**, then 0.71 m (its second difference; running on the flat:
     0.018 m) — its framing for hanging and climbing taken for a mantle's too;
   * its sprint framing (the shoulder's offset halved, the distance, the field of view) followed the body's speed frame
     by frame: at 9.7 m/s it was a sprint for four frames — **0.22 m sideways in a frame**.
3. **The hang at rest** was Mixamo's "Braced Hang" (decision 0336), looped from its last 60 %: a captured hang shifts
   its grip and weight, which over the traversal's fixed hands and braced feet reads as twitching.
4. **Limbs through the walls** (`anim_quality.gd`, new: each frame a ray from the hips to the head, elbows, knees,
   ankles and balls of the feet; a surface more than 4 cm before the joint is a limb through it): from hold to hold
   12.1 % of the frames with the mannequin's clips, **17.4 %** with Mixamo's; hanging 8.3 %, the deepest 27 cm.
   * A move between holds put the feet on the plane of the wall at the capsule's side (the hang's own on the stone
     found, the ankle 12 cm off it): the ankle on the wall, the ball of the foot and the toes in it.
   * Braced under a band that stands out (CasaCrema's, 14 cm proud of its front 20 cm over the foot) the foot went in
     under it and the shin through it: the 27 cm.
   * With the ankle 12 cm off the stone a flat foot's ball (15 cm from the ankle) was 3 cm in.
5. **The climb's smoothness**: Mixamo's "Climbing Up Wall" looped in place and its hops played over the moves' times
   (decision 0336) — within the gates (from hold to hold 2,130°/s, the gate 2,200), not measured further; the owner had
   played the mannequin's own climb before without this complaint.

## Change

* **The mannequin's own fall, hangs and climb** (`MOCAP_KEEP_OWN` in the player): Mixamo's clips are kept for the
  idle, the gaits, the jumps, the landings and the roll.
* **A step up's way over the top** (the engine, `_path_mantle()`): held off the wall until the feet are within a fifth
  of the rise of the top (at most 0.3 m); the hold eased in (the clip's way less a soft ramp of how far it is past the
  wall's); let go by blending from the held way into the clip's from no speed, over as long as the way it has to make
  up takes at 2 m/s more than the clip's own speed (the rest of the mantle at most).
* **The camera** (`rooftops_camera.gd`): the hang's and climb's framing (half way to level, 0.5 m closer) eases in and
  out over half a second and is not a mantle's (it had switched at once, a mantle's too); the sprint's framing, the
  lead and the lag follow the speed smoothed over a fifth of a second and held through a mantle or a vault.
* **Braced feet** (the engine, `_hang_foot()`): the stone also looked for 13 and 26 cm above the foot, the foot out as
  far as the nearest; the ankle 0.15 m off it; the feet of every move between holds placed so (they had been on the
  face's plane at the capsule's side).

## Measurements (headless)

* The fountain, from 3.5 m and 6 m away at a run, and from the side: the body across the rim at most **9.7 → 5.0 m/s**;
  the camera's largest jerk **0.77 m and 11° → 0.013 m and 0.1°** (the framing eased: 0.22 m; and the speed smoothed:
  0.022 m; and the mantle's way: 0.013 m; running on the flat: 0.018 m); no limb through the rim (0 % of the mantle's
  frames, as before).
* The traversal's step a frame (`climb_check.gd`, the debug editor build): 4.7 → 5.9 µs (the rays above the feet); every
  lane of `climb_check.gd` and `traversal_check.gd` as before; the regression suite's 40 scenes identical.
* `anim_quality.py` (the mannequin's hangs and climb, the climb up CasaCrema): limbs through the walls hanging
  **8.3 → 2.1 %** (deepest 20.5 → 4.4 cm), from hold to hold **12.1 → 2.6 %** (27.3 → 6.0 cm; Mixamo's: 17.4 %); the
  mantle onto the roof 0 % as before, its hips' fastest change 222 → 190 m/s² (the gate 400); **30 of 38 gates** as
  milestone 194's.
* Not changed: the jump at a wall's knee (8.8 % of the approach's frames, 11 cm into the wall as it is caught), the
  scramble up the tiles' toes (3.9 %, 9 cm).

## How it got there

* The hold kept and the clip's way smoothed by repeated averaging, the hold put back after each pass (the fountain's
  body 4.7 m/s at most), and the feet allowed over at 0.3 of the rise: on the climb up CasaCrema the body left the hang
  with a kink (565 m/s² at the hips, the gate 400) and the toes went into the eave (3.3 % of the mantle). The analytic
  ease in and out instead; a fifth of the rise.
* Over the top at 0.15-0.45 of the rise: at 0.3 and more a foot's toes in the rim (5-11 % of the mantle).
* A smootherstep for the letting go: 5.9 m/s at most (its steepest 1.875 times its mean, a smoothstep's 1.5).
* The feet of the moves between holds on the stone found, without looking above the foot: worse (14.8 %) — the bands
  above the feet were the cause.

## Decision

KEEP. The fall, the hangs and the climb are the mannequin's again; Mixamo's where it reads as better (the owner's
judgement in play: the idle, the gaits, the jumps, the landings). The mantle and the braced feet are FellowTraversal3D's
behaviour for every scene that uses it (it does nothing without the node).

## Rejected / not done

* Mixamo's hang with its grip shifts damped, its climb slowed: the traversal's hands and feet are where the ledge is,
  a captured hang's weight shifts against them; the mannequin's own were made for them.
* The knee of the jump at a wall (the mannequin's `jump_wall`) and the toes on the tiles: not what the owner saw; open.
