# FellowEngine — milestone 158 report (the reference's house finished; its pelvis)

Status: **FINAL** (2026-11-20). Branch `fellow/geometry`. Everything ran in the cloud (CPU, lavapipe); the laptop's GPU
stays paused at the owner's request.

## 0. Result

The owner, seeing milestone 157's shot beside the video: "why waste time on the animations when our models, even the
buildings, are clearly far from the reference's quality?" Then, of the mannequin: "why does its backside stick out
so?" and "I think there's a problem with the pelvis's deformation — remember I want 100 % agreement frame by frame".

* **The buildings** (decision 0298):
  * the stone in relief by parallax from Poly Haven's height maps, the blocks at the video's size;
  * louvred shutters and wooden casements on the windows;
  * real coppi over every roof face, with the roofs' collision following them;
  * carved scroll corbels under the balconette;
  * a thin zinc gutter and dark glass;
  * a softer grade, and the video's depth of field on the reference camera.
* **The pelvis** (decision 0299):
  * the fit's pelvis had drifted up to 52° from the trunk and the thighs to 168° from it, since the landmarks cannot
    say how far it tips. Held to a body's reach, the spine's bend is now -7…+21° and the hips' flexion at most 129°,
    with the landmarks fitted as well as before;
  * the mannequin's pelvis is as the video's: shallower behind, smaller buttocks, the briefs cut higher.
* **The comparison tool**: `showcase/tools/reference_look.gd` renders the reference shot at the video's frames, each
  after the light has settled, and sets them beside the video's.

## 1. Checks (cloud)

* The reference shot beside the video, at frames 10, 45, 70, 84 and 93 over five rounds, then at 25, 53, 57, 89 and 93.
* The pelvis per frame, fit by fit (decision 0299's table); the fitted mannequin over the video's frames.
* `anim_quality.py`: 38 of 38 gates, no regression against this milestone's first baseline.
* The scene builds without errors. There is no engine code in this milestone.

## 2. Next

* **The light** (the owner: "why does our lighting look so nasty, instead of soft and delicate as in the
  reference?"). Measured on five frames: ours is darker (median luminance 0.024-0.08 against 0.08-0.18), its shade 2-3
  times darker, its fine detail 1.3-1.6 times as contrasty, its colour more saturated (106-131 against 87-90).
* **The camera and the house measured again**: a camera solved from the video's background (structure from motion,
  0.5 px over 11 700 tracked points) cranes straight up the house rather than in along it. Through it the house is
  taller than the replica: the balconette's top about 2.3 m up, the eave about 5 m. The replica and the performance
  are to be measured through that camera, frame by frame.
* The GPU, when the owner allows it.
