# 0344 — The joints within a person's range of motion: measured, then kept there (`FellowJointLimits3D`)

Status: **KEEP** · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_joint_limits_3d.{h,cpp}` (new): `FellowJointLimits3D`, a `SkeletonModifier3D` last
  among a humanoid skeleton's modifiers — the wrists (`limit_wrists`, `wrist_flexion_range`, `wrist_deviation_range`,
  `wrist_pivot`) and the ankles (`limit_ankles`, `ankle_range`) within their range, then the elbows and knees bent in
  their hinges' plane (`hinge_alignment`, `hinge_max_turn`), then the head against the chest (`limit_neck`,
  `neck_flexion_range`, `neck_bend_limit`, `neck_turn_limit`); each correction eased at `max_correction_speed`;
  `bone_names`, `get_bones()`, `get_stats()`;
* `showcase/tools/anatomy.gd` (new): the posed skeleton's joints in anatomical terms against an adult's active range of
  motion; `showcase/tools/anim_quality.{gd,py}`: each segment's share of frames out of range, its worst moments, seven
  gates and an anatomy table (`docs/showcase_animation_quality.md`);
* `showcase/scripts/rooftops_player.gd`: the player's skeleton limited (`JOINT_LIMITS`; `--joint_limits=0`,
  `--hinge_alignment=0`, `--limit_ankles=0`, `--limit_wrists=0`, `--limit_neck=0`, `--max_correction_speed=`).

## Problem

The owner, after milestone 201: "assess the movements' anatomical correctness, run a quality loop and deliver something
extra". `anim_quality.py` measured the knees' and hips' curves against gait studies, the joints' speed and the limbs
through walls — not whether a pose is one a person can take.

## The measure (`tools/anatomy.gd`)

Read from the skeleton as drawn (after every modifier), each frame: the spine (the upper chest against the hips) and
the neck (the head against the upper chest) — flexion, bend, turn; the shoulder — how far the upper arm goes behind the
chest's plane and across its middle; the elbow and the knee — the hinge's flexion in the bone's own frame (so the upper
arm's or thigh's roll turns it, as a person's does) and its bend to the side (a hinge has none); the forearm's turn
(supination to pronation); the wrist — extension and flexion, radial and ulnar deviation (the hand's axis to the middle
finger's knuckle, its thumb side between the index and the little finger's); the hip — flexion, abduction; the ankle —
plantar- and dorsiflexion; the body's own volume — an elbow or a wrist inside the trunk, the knees or the ankles through
each other. The limits are an adult's active range of motion (AAOS's, a few degrees' slack: the elbow −10..150°, its
side 25°, the knee −10..155°, its side 20°, the wrist −85..75° by −40..25°, the ankle −30..55°, the neck −70..60°,
±45°, ±85°, the hip −30..140°, −30..50°). A measure past its limit by more than 0.5° counts the frame.

## What was found (the rooftops' demo at 120 fps, milestone 201's motion: the limits off)

| segment | frames out of range (any joint) | the worst |
|---|---|---|
| hanging | 52 % | the knees bent sideways in 52 % (the left 47° at the catch — 27° past a hinge's slack); the elbows 25 %; the left hip spread 58° (27 %); the right wrist's deviation 12 % |
| from hold to hold | 26 % | the elbows bent sideways in 19-26 % (both up to 82° — the arm's IK placing the elbow with the arm's turn the clip's); the left ankle dorsiflexed 52° (14 %); the right knee 41° sideways; the head tipped back 81° (11 %); in the across move the left wrist bent back 169° and 106° to the thumb's side |
| the mantle | 13 % | the left ankle pointed 64° |
| the roof | 89 % | the head tipped back 83° on the scramble up the tiles |
| the gaits | up to 5 % | a wrist's deviation (the run's start), a knee sideways (the turn), an ankle (the walk) |

`anim_quality.py`: climbing's hinges out of their plane in 52 % of a segment's frames, the wrists 12.5 %, the ankles
14.4 %, the neck 89.2 %; the gaits (but the hips) 4.8 %.

## Change

* **Hinges**: an elbow or a knee that the IK (`FellowTraversalPose3D`'s arms and legs, `FellowFootPlacement3D`) places
  keeps the upper arm's or the thigh's turn as the clip had it, so its hinge bends sideways. The modifier turns the upper
  bone about its own line (only the shoulder's or the hip's rotation changes — the elbow, the knee and the lower limb
  stay where they are) until the hinge lies in the bend the limb has. A limb near straight (under 8° of bend, fully from
  20°) keeps the turn it had: its plane no longer says which way. One bent backwards, further round than
  `hinge_max_turn` (120°), is left.
* **Wrists**: the hand brought back along the way it is bent to an ellipse through the four limits, turned about its
  middle knuckle (`wrist_pivot`): the forearm and the upper arm reach the wrist's new place (a two-bone solve keeping
  their bend's plane), so a hand on a hold stays on it while the forearm swings across.
* **Ankles**: the foot turned about the ankle in the plane of the shin and the foot.
* **Neck**: the head against the chest, about the chest's own axes.
* **Order and easing**: wrists and ankles, then hinges (a wrist turned about its knuckles moves the arm), then the neck.
  Each correction is a rotation in its bone's own frame, eased on and off at 720 °/s. A hinge's turn eases at a quarter
  of that near straight, rising to the full speed at 48° of bend. The wrists' corrections apply at once, because they
  undo the hand's own turn as it comes.
* The showcase's player gets one, last among its skeleton's modifiers.

## Measurements (headless; the demo at 120 fps, `anim_quality.py`)

| | the limits off | **the limits on** |
|---|---|---|
| climbing: elbows and knees out of their plane (the worst segment) | 52.1 % (hanging) | **0.4 %** (from hold to hold: an elbow 27°, 2° past) |
| climbing: wrists; ankles; neck | 12.5 %; 14.4 %; 89.2 % | **0 %; 0 %; 0 %** |
| climbing: spine and shoulders; limbs in the body | 0 %; 0 % | 0 %; 0 % |
| the gaits: every joint but the hips | 4.8 % | **0 %** |
| `anim_quality.py` gates | 33 / 45 | **38 / 45** |
| the fastest joint: hanging; from hold to hold (gate 2,200 °/s) | 959; 2,000 °/s | 1,004; 2,026 °/s |
| climbing: settled held hands' fingers off the edge; no held hand's on it | 0.2 %; 0.0 % | 1.1 %; 1.2 % |
| the hang program: the same | 0.0 %; 0.0 % | 0.0 %; 0.0 % |
| limbs through walls from hold to hold | 0.7 % | 1.0 % (the left toes) |
| what it corrects over the demo's 14 s | — | hinges turned up to 83°, wrists 80°, ankles 22°, the neck 13° |
| its time (a debug build, the cloud's CPU) | — | 7 µs a frame |

The jump at a wall (milestone 201) is the same to the centimetre. `climb_check.gd` (2 of 37 lanes) and
`traversal_check.gd` (4 of 19) differ slightly from milestone 201's, with no failures: registering a class changes the
instance IDs that stagger `FellowTraversal3D`'s checks (`instance_id % check_interval`). The same build with the class
unregistered gives every lane identical.

## How it got there

* The measure first: the bends' `atan2` grew under deep flexion (a deeply bent hip or neck read as bent sideways) →
  `asin` of the side's component; arms over the head no longer "across" the chest; the hip's flexion limit 140° (a person's deep
  squat); 0.5° of slack.
* The wrists' first clamp flipped a hand over (one frame 20,272 °/s) → brought back along the way they are bent to an
  ellipse. Eased, they lagged the hand's turn → at once.
* Hinges turned at once snapped as a limb straightened and its plane flipped (2,865-4,014 °/s) → each bone's
  correction eased, held near straight, slower near straight (720 °/s alone: 2,320; 450 °/s: the hinge lagged its bend).
* The wrists' clamp turned the hand about the wrist, lifting the fingers off their holds (1.8 % of settled held hands'
  frames) → about the knuckles, the arm re-reaching (1.1 %).
* The re-reach bent a stretched arm's elbow sideways (1.4 % of the frames from hold to hold) → the wrists before the
  hinges (0.4 %).

## Decision

KEEP: `FellowJointLimits3D` (a node: nothing changes without it); the showcase's player limited.

## Rejected / not done

* **The hips**: hanging's caught left leg spread 58° (8° past; 27 % of the hang's frames, the catch's clip), the mantle's
  and the crawl's hips flexed to 147-151° (8 %, 7 %), the gaits' push-off extended to −34..−41° (up to 17.5 % of a
  start's frames: the clips' own stride). A hip limit moves the knee and the foot: the feet the IK put on the wall or the
  ground would leave them. Left to the clips (a retargeting fix) or a later leg re-solve.
* **The across move's trailing hand**: with its arm straight, its wrist's correction lifts the fingers off the edge for
  four frames (settled held hands' fingers off the edge 0.2 → 1.1 %). Accepted against a wrist bent back 169°.
* A full joint limit (swing-twist cones on every bone, as a ragdoll's): the pose's IK and clips put the shoulders and
  the spine within their range already (0 %); the cones would add cost and fight the IK where it is right.
