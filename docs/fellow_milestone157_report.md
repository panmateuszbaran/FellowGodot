# FellowEngine — milestone 157 report (the owner's reference video performed 1:1)

Status: **FINAL** (2026-11-19). Branch `fellow/geometry`. Everything ran in the cloud (CPU, lavapipe); the laptop's GPU
stays paused at the owner's request.

## 0. Result

The owner, of milestone 156: "and where is the 1:1 reproduction of the reference's animations?" — milestone 156's
clips were the generator's, reshaped by eye. Now the video's motion is measured and replayed (decision 0297).

* **Measured** (in the cloud; MediaPipe Pose, OpenCV and SciPy stay outside the repository, as the owner chose): the
  pose landmarks on all 96 frames; the camera's turn and focal length from the city's lines (1300 px, 52.4° across);
  the mannequin's own skeleton fitted to every frame at once through that camera (the shoulders', elbows', wrists',
  hips', knees' and ankles' landmarks 7.2 px off on average at 1280x720); the holds from the optical flow around each
  hand and foot (24); the hips' path from the holds (the run 3.4-3.6 m/s, the leap from 1.5 m out, the balconette
  1.70 m up, the eave's lip 3.25 m up), the replica's surfaces pinning how far out each hold is.
* **Kept** (`showcase/tools/blender/reference_performance.json`, 62 KB): per frame of the video every body segment's
  rotation and the hips' place, the camera, the holds and their places on the replica.
* **Replayed**: `mannequin_reference.py` bakes the clip `reference` (3.96 s at 60 Hz, the holds kept still by IK; the
  fastest joint 1517°/s, the median frame's 556°/s) and the camera's path into `ReferenceShot.glb`; the video's house
  (`CasaRiferimento`: ashlar, the balconette on carved corbels, the window, the eave, the gable roof) closes the
  square's west street; the demo's second shot, `reference`, plays the performance through the video's camera.
* **Compared**: `reference157_sbs.mp4` — the video and FellowEngine's render of the same performance, frame for frame.

## 1. Checks (cloud)

* The fitted mannequin rendered from the measured camera over each of the 96 frames (Cycles): `fit2_sbs.mp4`.
* The baked clip's fastest joint per frame (decision 0297's table); the IK's pops found and fixed (an arm turned over
  at 9695°/s; a wrist twisted round; a forearm's twist wrapping at half a turn).
* The scene builds without errors; the shot rendered with Movie Maker at 24 fps (lavapipe, 1600x900, 400 s).
* The player, its clips, the traversal and the engine are unchanged (no engine code in this milestone).

## 2. Next

* **The buildings** (the owner, seeing the comparison: "our models, even the buildings, are far from the reference's
  quality"): the masonry's relief (blocks and joints in the geometry or by parallax), the windows' mouldings, louvred
  shutters, glass with reflections and dark rooms, the tiles over the whole roofs, carved corbels, weathering; the
  camera's depth of field and motion blur as the video's. Measured the same way: the engine's frame beside the video's.
* The game's own clips from the measured performance (the run, the leap at the wall, the climb by the balconette, the
  hang, the mantle, the scramble).
* The GPU, when the owner allows it: `f518`, then decision 0270's closed frame.
