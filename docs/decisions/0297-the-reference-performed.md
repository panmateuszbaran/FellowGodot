# 0297 — The owner's reference performed 1:1: its motion measured from the video, replayed at a replica of its house

Status: **KEEP** (the showcase's: a data file, a Blender module, a house, a scene node and a demo shot; nothing in the
engine) · Branch `fellow/geometry` · Code: `showcase/tools/blender/reference_performance.json` (the measured
performance), `mannequin_reference.py` (the clip and the camera from it), `build_mannequin.py` (`ReferenceShot.glb`),
`ac_buildings.py` (`casa_riferimento()`), `build_rooftops_models.py`; `showcase/tools/build_rooftops.gd`
(`CasaRiferimento`'s place, `_reference_shot()`), `showcase/scripts/rooftops_demo.gd` (the `reference` shot,
`--demo_only`) · Checks: the fitted skeleton drawn over the reference's frames; the baked clip's fastest joint per
frame; the shot rendered by FellowEngine beside the reference, frame for frame.

## Problem

Milestone 156 (decision 0296) rebuilt the mannequin as the reference video's and reshaped its procedural run and
roof scramble by eye, from frames. The owner: "and where is the 1:1 reproduction of the reference's animations?" — it
was not one: the clips were the generator's, their poses and timing only nudged towards the video's. The owner chose
how to measure it: MediaPipe Pose (Google's pose estimator, Apache 2.0), run in the cloud only, nothing of it in the
repository.

## Change

**Measuring the video** (in the cloud, once; the scripts and MediaPipe stay outside the repository):

* MediaPipe's pose landmarker (`pose_landmarker_heavy`) on each of the 96 frames (1280x720, 24 fps): 33 landmarks in
  the picture and in 3D, every frame found. One frame's legs had their sides swapped (the 6th: the labelling that
  moves least from frame to frame, by dynamic programming over the arms and the legs).
* **The camera**: the city's lines (OpenCV's line detector away from the figure) give each frame's vertical and the
  houses' two horizontal directions (a Manhattan world) and the focal length that makes them agree best over the
  video — 1300 px, a horizontal field of view of 52.4°. The camera rises with the climber: level while it runs,
  looking up 18° under the climb, level again over the roof. Where the lines are few or mislead (the leap at the wall;
  the roof's tiles slope), the camera's turn about the vertical is held by the climber's heading, smoothed.
* **The skeleton** (`scipy.optimize.least_squares`): the mannequin's own skeleton (its bones' lengths, its hinged
  elbows and knees) fitted to all 96 frames at once — 40 parameters a frame (the hips' place and turn, the trunk's,
  the head's, each limb's) through a perspective camera with that focal length; MediaPipe's 3D directions a weak prior
  for the depth; the joints' accelerations and the limbs' turns kept smooth; a foot turned no more than 25° about the
  shin from the knee's hinge. The landmarks' error: 7.2 px on average for the shoulders, elbows, wrists, hips, knees
  and ankles (the 95th percentile 20 px). A nearly straight limb takes its bend's direction from the frames around it
  (a straight knee or elbow had turned over between frames: 4000°/s).
* **The contacts**: which hand or foot holds still, from the picture — each one's motion against the optical flow of
  the surface just around it (the body masked out by the fitted mannequin's silhouette): 24 holds (the run's four
  steps, the foot on the wall, the hand on the window's balconette, the foot on it, the hands on the eave, the feet in
  the window, the hand and the feet on the tiles).
* **The path**: the hips' place in the world solved at once (linear least squares): every hold still, the ground flat,
  gravity in the air (the run's three flights, the leap), the hips smooth elsewhere; the balconette and the eave each
  one surface for every hold on them. The run 3.4-3.6 m/s with the hips 0.86 m up, a leap from 1.5 m out to the wall,
  the balconette's top 1.70 m up, the eave's lip 3.25 m up. How far out from the wall a hold is the picture tells
  least (the camera looks at the wall from below and behind the climber): there the replica's surfaces pin the holds —
  the wall, the balconette (0.40 m out), the window's inner sill, the eave's lip (0.25 m out: with the 0.47 m the free
  solution gave, the hanging body had been inside the eave, where the video shows it hanging in front of the gutter),
  the tiles behind it — and the hips' path follows them.
* The left hand, hidden behind the body while the climber hangs from the eave (frames 64-83; MediaPipe put it 0.37 m
  off the lip), is the right one mirrored there.

**In the repository**:

* `reference_performance.json` (62 KB): per video frame each body segment's rotation and the hips' place, the camera's
  place and turn, the holds and where they go on the replica, the hands' grip — in the replica house's frame.
* `mannequin_reference.py`: the clip `reference` (3.96 s at 60 Hz): the segments' rotations and the hips' path through
  Catmull-Rom splines; the trunk's turn spread over the spine's three bones, the head's over the neck, the hand's twist
  shared with the forearm (unwrapped over the frames); each hold kept still on its stone by a two-bone IK whose
  correction is smoothed over a twentieth of a second (taken or let go in a frame, an arm had turned 160° in it), the
  bend kept as the pose had it, a nearly straight limb bending the way it bent the frame before. The camera's path
  smoothed (its place came through the fit's depth). Both in `ReferenceShot.glb`, one animation.
* `ac_buildings.casa_riferimento()`: the reference's house to its measures — a low house of sandstone ashlar, an arched
  door, a window with a plain sill and, near the right corner, the climbed window over a balconette on three carved
  corbels, a stone bench along the front, the eave's rafters straight out of the wall (no cornice: the window reaches
  up to it) 0.25 m, a slim gutter under its lip, a gable roof at 22°.
* The rooftops scene: `CasaRiferimento` closes the square's west street (its walkers go by the alley north of it);
  `ReferenceShot` (the mannequin, the camera, their clip) stands with it, hidden. The demo's second shot, `reference`,
  shows it: the clip from its start, its camera current, the player and the square's people hidden, the sun turned to
  light the house's front from its left, low, as the video's does (the square's golden hour leaves its west side in
  shade). `--demo_only=<shot>` plays one shot.

## Checks

* **The fit beside the video**: the fitted mannequin rendered from the measured camera over each of the 96 frames
  (Cycles): its silhouette on the reference's.
* **The clip's smoothness** (the fastest joint each frame at 60 Hz): median 556°/s, the 95th percentile 1138°/s, the
  fastest 1517°/s (the right forearm reaching for the balconette in the leap; milestone 156's gates: 2200°/s). Before
  the IK's corrections were smoothed and its bend kept, the IK had turned an arm over: 9695°/s.
* **The shot beside the video** (FellowEngine on lavapipe, Movie Maker at 24 fps): `reference157_sbs.mp4`.
* The scene builds without errors; the gameplay's checks are unaffected (the player, its clips and the traversal are
  unchanged).

## Decision

KEEP: the reference's motion is now the video's, measured, not the generator's — on the replica of its house, through
its camera.

## Rejected / not done

* Copying the video's city: the replica is the house the climber uses, in the showcase's own square; behind the
  runner the square's other side is ours.
* The roof's 0.5 s: the camera's turn there is the least certain (the tiles' slope lines mislead the vanishing points;
  held by the climber's heading); the hands' and feet's holds are put on the replica's tiles by IK.
* The buildings' finish: beside the video the replica (and the square's houses) is plainly poorer — flat walls under a
  photograph of stone, the shutters and frames plain boxes, tiles only along the eaves. The owner, seeing it: the
  buildings next.
* The hidden left hand's measured holds (on the window's jamb, on the eave): pulled there by IK its elbow turned over;
  its arm mirrored from the right instead.
* The game's own clips from the video (the run, the leap at the wall, the climb by the balconette, the hang, the
  mantle, the scramble): next — this record's data is their source.
