# 0296 — The owner's reference: the mannequin as in the video, its run and its scramble up the tiles, no pops

Status: **KEEP** (engine: `FellowTraversalPose3D.clip_blend`, `pole_follow` and `max_joint_speed` are off by default;
`FellowTraversal3D`'s mantle with a clip's root motion is let go from the wall over 18 % of it — a fix; `FellowFootLock3D`
no longer eases a foot back from beyond the leg's reach — a fix; the rest is the showcase's) · Branch `fellow/geometry` ·
Code: `modules/fellow_animation/fellow_traversal_pose_3d.{h,cpp}` (`_limit_speed()`, `_snapshot()`, `_two_bone()`),
`fellow_traversal_3d.cpp` (`_path_mantle()`, `MANTLE_RELEASE`), `fellow_foot_lock_3d.cpp` (`REACH_SHARE`); the
showcase's model (`tools/blender/mannequin.py`), clips (`tools/blender/mannequin_anims.py`: `gait_pose()`,
`crawl_pose()`, `_strike()`; `tools/blender/mannequin_rig.py`: `_mixed()`), scenes (`tools/build_rooftops.gd`), player
(`scripts/rooftops_player.gd`: the crawl) and demo (`scripts/rooftops_demo.gd`: the `orbit` camera, the house's shots) ·
Checks: `showcase/tools/anim_quality.{gd,py}` (38 gates; 6 new), `bench/climb_check.gd`, `bench/traversal_check.gd`, the
regression scenes, renders beside the reference's frames.

## Problem

The owner, of the rooftops showcase: "I'd rather this scene looked more like this" — a 4-second video (24 fps,
1280x720): a grey articulated mannequin running across a sunlit square of stone houses, up a house's front by its
sills, hanging from the eave, over the gutter and up the tiles on hands and feet; then: "and don't finish until the
character's model is identical to the one in the reference video and the animations are as smooth and realistic as
in it".

Compared, frame by frame:

* **The model.** Ours (milestones 150-155): one smooth torso from the crotch to the neck, slim limbs (the thigh 16 cm
  across at its top), cream-white, a dark ball at every joint, a small egg of a head with a brow and a nose. The
  reference's is an artist's mannequin: a breastplate to the bottom of the ribs (broad, flat pectorals), the abdomen,
  a pelvis cut like briefs — plates with thin dark seams between them; a large egg of a head, the chin forward, a soft
  plane for a face; a dark conical neck; the arm's top a grey dome in a dark socket of the breastplate; dark elbows
  and wrists; the knee a grey ball between dark rings; dark ankles; feet like smooth shoes; heavy thighs and calves;
  mid-grey, satin.
* **The run.** Ours, measured on the clips: the trunk 5.8° forward running (10.2° sprinting), the reference's some
  20°; the forward hand up by the face (the wrist 2.8 cm over the shoulder; 6.2 sprinting) and the forearm across
  the chest (the upper arm's twist had turned the elbow's plane inwards by up to 30°), a thumb sticking up.
* **The roof.** No scramble up the tiles: the body stood up on the roof.
* **Smoothness.** Measured for the first time (below: the fastest joint each frame, its turn against its parent): the
  run's jump at a wall turned an arm 42° in a frame (5000°/s; the clip swapped the arm from FK to IK halfway between
  two keys); the mantle from a hang snapped the braced legs to the clip's first pose (a knee 8863°/s) and moved the
  body 0.65 m in two frames over the eave (the hips' acceleration 4650 m/s²: the traversal held the body at the wall
  until the clip's root was over the top, then let go at once); a leap turned a forearm 53° about its own axis in a
  frame (6405°/s); the run from standing straightened the trailing knee and bent it 23° in a frame (the foot lock
  eased the foot back from beyond the leg's reach). A sprinting knee's fastest is some 1100°/s.
* **The look** (`look_compare.py`: CIELAB statistics of three of our views against the reference's frames at 0.3,
  2.0 and 3.8 s): plastered houses where the reference has stone, the shade too dark and too yellow.

## Change

* **The model** (`mannequin.py`, `build_mesh()`, on the same skeleton — the clips and the IK unchanged): the torso one
  surface (a Catmull-Rom table of sections from the crotch to the neck) cut into three plates at the waistband
  (0.985 m) and the bottom of the ribs (1.148 m), 14 mm apart, their edges rolled in by 4 mm over a dark core 2.5 mm
  inside them (the core seen some 9 mm wide between them, as the reference's seams measure at its scale), the seams
  lower in front (the breastplate's edge by 2.8 cm, the waistband by 1.4 cm); the pectorals and shoulder blades, the
  spine's groove and the buttocks sculpted onto it; the pelvis's lower edge rising from the crotch to the hips (the legs' openings);
  the head an egg (23.7 cm tall, 17 cm wide, the chin 7 cm forward of the crown's line) with a flattened face; a dark
  conical neck; a dark socket for the arm's grey dome; dark balls at the elbows, wrists and ankles; the knee a grey
  ball with dark rings; the limbs heavier (the thigh 20 cm across at its top, the calf 12 cm deep); feet like shoes;
  the shell mid-grey (the scene's material 0.55 sRGB, roughness 0.5, a light clearcoat), the joints and the core near
  black (0.07). 15134 vertices (7604): two materials as before. The faces' winding checked part by part (every
  one of the 59 closes with a positive volume: the head and the rings had been inside out, drawn black in Godot).
* **The run** (`gait_pose()`): the lean 18° jogging, 32° running, 36° sprinting (trunk 10.6°, 19.7°, 22.2°), the head
  held up by 30 % of it, not 40 %; running, the upper arm swings further back than forward (-9° offset, the swing
  39-46°), the twist 4-9° (the forearm in front of its own shoulder: the wrist 16.8 cm from the body's middle, at
  most 25 cm under the shoulder), the hands half closed with the thumb along the fingers.
* **The scramble** (`crawl_pose()`, the clip `crawl`): up a 21° roof on hands and feet in a lateral sequence (left
  hand, right foot, right hand, left foot), the trunk nearly along the slope, the head up, the palms flat on the tiles,
  on the balls of the feet; 0.55 m/s, 1.1 s a cycle. The player (`rooftops_player.gd`) scrambles on a roof steeper than
  15° going up it slowly (walking, or the demo's `want_crawl`) at `crawl_speed` (0.65 m/s; the clip at the rate of the
  speed, the feet not locked).
* **No pops.**
  * The clips' rig (`mannequin_rig.py`): a limb going from FK to IK between two keys is posed both ways and its bones'
    turns blended (it had been swapped halfway).
  * `FellowTraversalPose3D.clip_blend` (off; the showcase 0.25 s): a clip begun after the limbs were posed by IK (a
    mantle from a hang) fades in from this node's last pose.
  * `FellowTraversal3D`, a mantle with a clip's root motion: held at the wall up to the last of the clip's samples
    still below the top, then let go over 18 % of the mantle (`MANTLE_RELEASE`), not at once.
  * `FellowTraversalPose3D.pole_follow` (off; the showcase 1.0): the elbows' and knees' bend follows the clip's own as
    much as the pole's — where a target passes the pole's line the pole's direction across the limb turns over in a
    frame.
  * `FellowTraversalPose3D.max_joint_speed` (off; the showcase 2000°/s): while the traversal poses the body, no bone
    turns faster than this from the node's last pose — what still turns over in a frame is spread over the frames it
    needs.
  * `FellowFootLock3D`: a foot let go is never eased back from further from the hip than the leg reaches (99 % of it,
    or as far as the animation puts the foot).
* **The gaits' last gates** (decision 0295's): the walk's hips 8 mm higher (its knee at the strike 20° → 15°: in its
  band); the foot flat by 14 % of the stance, not 20 % (the ankle's plantarflexion in the loading 8.9° → 13.5°); the
  sprint's knee 34° at the toe-off (its least 12.7° → 15.9°).
* **The look** (`build_rooftops.gd`): the houses' plaster replaced by sandstone ashlar (Poly Haven's
  `sandstone_blocks_05`, CC0, `fetch_textures.py`; blocks some 45 by 25 cm, each house its shade), the ground floors'
  rustication of the same stone, the square's large slabs; the shade lighter (the ambient light 1.35, SDFGI 1.25), the
  colours not boosted (saturation 1.0).
* **The demo** (`rooftops_demo.gd`): the house's shots filmed as the reference is — a camera in the character's frame
  (`orbit`): beside the runner, a little ahead, at the chest's height; close behind and below the climber, looking up
  the wall; beside the body on the tiles at the roof's height; then the camera rising over the roofs. After the mantle,
  up the tiles on hands and feet for 3 s.

## Checks

* **`anim_quality.py`** (the bones after the modifiers at 120 Hz; milestone 155's last build as the baseline): 38 of 38
  gates pass (milestone 155: 29 of 32). The new gates, the fastest joint per climbing segment (≤ 2200°/s) and the
  mantle's body (the hips' acceleration ≤ 400 m/s²):

  | | the first build (the new model and clips, milestone 155's engine) | now |
  |---|---|---|
  | the run and the jump at the wall | 5001°/s | 2086°/s |
  | hanging | 1750 | 1753 |
  | hold to hold | 4428 | 2087 |
  | mantle | 8863 (the hips: 4653 m/s²) | 2000 (197 m/s²) |
  | the scramble up the roof | 2098 | 1371 |
  | the run from standing (not gated) | 2804 | 1343 |

  The gaits' other gates no worse than milestone 155's (the walk's skate 0.12 cm a stance; the changes of gait 1.6-2.1
  cm).
* **`climb_check.gd`**: the 37 lanes' events, places, times and errors value for value on GodotPhysics3D and Jolt.
* **`traversal_check.gd`**: the 19 lanes (with `clip_low`, a mantle by a clip's root motion) value for value against
  the build without `MANTLE_RELEASE`.
* **Regression suite**: `climb`, `ledge_corners`, `traversal` exact.
* **The class reference**: `clip_blend`, `pole_follow`, `max_joint_speed` and the foot lock's way back documented, in
  `--doctool`'s order; `xmllint` and `make_rst.py` clean.
* **The look** (`look_compare.py`, CIELAB over each view, lavapipe; the views as near the reference's moments as the
  demo comes: the run on the square, the climb on CasaCrema's front, the roof):

  | view | metric | reference | milestone 155's city | now |
  |---|---|---|---|---|
  | the climb (b) | lightness L* / its spread | 47.7 / 22.7 | 53.6 / 19.4 | 44.0 / 20.2 |
  | | warmth b* / colourfulness C* | 16.2 / 17.8 | 20.5 / 24.7 | 16.7 / 20.7 |
  | | dark share (L* < 20) | 9.4 % | 10.0 % | 18.9 % |
  | the roof (c) | L* / b* / C* | 48.4 / 13.7 / 16.7 | (hanging under the eave) 35.3 / 20.5 / 22.8 | (on the tiles) 32.4 / 16.7 / 23.4 |
  | the square (a) | L* / b* / C* | 36.6 / 9.3 / 11.8 | (another view) | 39.3 / 15.6 / 16.9 |

  The stone's warmth and colour nearer the reference's; still darker in the shade (the houses facing away from the low
  sun, the tiles' shadows) and more colourful.
* **Pictures**: the model in Blender (Cycles) beside the reference's frames 4 and 44; the run cycle beside the
  reference's first 32 frames; the scramble on a 21° slope; the scene's three views beside the reference's.

## Decision

KEEP.

## Rejected / not done

* **Pose estimation of the reference** (MediaPipe or the like, for its joints' curves): external code the owner has not
  approved; the reference was read by eye, frame by frame, and its run's lean, arm swing and stride compared on sheets
  of renders beside it.
* **The reference's proportions to the letter**: its mannequin is not the same from shot to shot (a generated video);
  the skeleton (and every clip, gate and IK on it) kept, the shells shaped over it.
* **Plates weighted each to one bone** (rigid, as a real mannequin's): the chest's plate turned against the abdomen's
  by the spine's 21° of running lean, opening the seam behind and closing it in front; weighted by height as before.
* **Discs for the knees and elbows** (grey caps on a dark ball, on the hinge's ends and the front): from in front they
  read as a cluster of balls; the knee is a grey ball between dark rings, the elbow a dark ball, as the reference's.
* **Seams 9 mm apart over a core 3.5 mm in, a shell of 0.66** (the first in the scene): in the sun the rims' bevels
  were lit and the seams read as light lines on a white body; 14 mm over a core 2.5 mm in, 0.55.
* **A joint speed limit of 1500°/s**: 5 % of the climbing's frames reached it (the hands late for their holds); 2000.
* **The hanging feet on a window's sill or beside it** (`FellowTraversal3D.hang_foot_search`, tried): CasaCrema's
  collision is its front's plane, without the windows' openings — the feet already braced on it (visually on the
  recessed glass, the legs near straight: the body hangs 0.52 m out from the wall under the pediment's lip); taken out
  again, unproven. Next: the body nearer the wall under a hold that stands out, the openings in the houses' collision.
* **The foot lock's hold kept within 98.5 % of the reach while locked**: the walk's planted foot let go at the heel's
  strike (its skate 0.006 → 0.9 cm a stance; walk to run 11.6 cm); only the way back is kept within the reach.
