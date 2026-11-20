# 0301 — The reference matchmoved: its camera from its own background, its house measured through it, its climber fitted in the world

Status: **KEEP** (the showcase's: its data, its replica house, its street; nothing in the engine) · Branch
`fellow/geometry` · Code: `showcase/tools/blender/reference_performance.json` (measured again, with the house's
measures in `"house"`), `ac_buildings.py` (`reference_house_measures()`, `casa_riferimento()` built from them),
`build_rooftops_models.py`, `showcase/tools/build_rooftops.gd` (its footprint, its flagged street, no posts or raised
pavement round it), `showcase/rooftops/shaders/paving_pbr.gdshader` (`flagged`) · Checks: the reference shot at the
video's frames with the video's edges drawn over it and the two blended; the clip's hips through its camera against the
fit's.

## Problem

The owner asked for agreement with the reference video "frame by frame, 100 %". Decision 0297's shot did not have it:

* **The camera** came from the vanishing points (its turn) and the fit's depth (its place: the camera where the
  climber's hips appear at their pixel). Its turn was least sure where the roof hides the walls. Its place inherited
  the monocular fit's depth, which wanders.
* **The house** was measured through that camera and that depth: the balconette 1.70 m up, the eave 3.25 m — while the
  climber hangs from the eave at full stretch with his feet on the sill, 1.55 m below it.

Beside the video, our shot's camera looked at the house from another side, and the roof frames were turned 30-40°.

## Change

All of the measuring is in the cloud (OpenCV, SciPy), outside the repository, as decision 0297's.

**The camera matchmoved from the video's background** (structure from motion):

* Tracks: corners tracked through the 96 frames (pyramidal Lucas-Kanade, checked forward and backward), the climber
  masked out by the fitted mannequin's silhouette.
* Cameras: two frames' essential matrix, each next frame by PnP on the points so far and new points triangulated, then
  a bundle adjustment of all of them — 96 cameras, 11 722 points, 208 772 observations, the focal length free.
* Result: every frame registered; the reprojection error 0.50 px median, 0.63 mean. The focal length stayed at the
  vanishing points' 1300 px.
* Placed in the world by the scene itself: up the ground's normal, x out of the climbed wall's face (their planes
  88.7° apart, the walls' relief σ 0.10 m, the ground's 2.6 cm), the ground at 0, the wall where it was.

The camera does not move in along the wall, as decision 0297's did. It tracks the runner, stops 2.1 m out from the
wall, cranes straight up from 0.6 m to 4.1 m, and turns down onto the roof. On the roof its turn is 30-40° off the old
one.

**The house measured through it:**

* The front rectified: every frame projected onto the wall's plane through its camera, the climber masked, the median
  taken, on a metric grid.
* The eave and the roof from the matchmove's points; the eave's grip from where the hands' pixels meet it.

The video's house, in metres:

| | |
|---|---|
| front | 6.64 m wide |
| door, near the left corner | arched, 1.06 × 1.66 m, in a frame of voussoirs |
| window over the door | 1.26 × 1.31 m, its sill 2.17 m up, louvred shutters open |
| climbed window, 1.6 m from the right corner | 0.88 × 0.96 m, its sill 2.01 m up, a stone frame, a casement, no shutters |
| its balconette | 1.46 m long, 0.28 m out, on two carved corbels, a mask between them |
| bench | under the climbed window |
| eave | the gutter 3.85 m up, the lip 0.48 m out |
| roof | a gable at 27° |

**The climber fitted in the world:**

* The mannequin's skeleton fitted to MediaPipe's landmarks over all 96 frames, through these cameras, held fixed.
* Its hips' place is in the world; its holds are each one point, on the replica's surfaces (the ground, the wall, the
  balconette, the sill, the gutter, the tiles).
* Gravity in the air; the body's ranges (decision 0299).
* The scene's one unknown: its scale against the mannequin, 0.808 of the matchmove's units. With that scale the house
  is self-consistent, and both the run's and the hang's frames fit.
* The landmarks' error is 8.5 px on the main joints; the camera-axes fit, with nothing in the world to agree with, had
  7.3.
* The clip baked from it: the hips through the baked camera fall within a few pixels of the fit's.

**In the repository:**

* `reference_performance.json`: the new performance, the camera and the holds, with the house's measures in it.
* `casa_riferimento()` built from those measures.
* The scene: the house's footprint; the street before it flagged like the video's (`paving_pbr`'s `flagged`
  rectangle: the square's slabs without its travertine bands); no posts across its street's mouth; no raised pavement
  round it (its runner's last steps are a metre from its front).

## Checks

The reference shot rendered by FellowEngine at the video's frames 10, 25, 45, 57, 72, 84, 93, with the video's edges
drawn over it and the two blended:

* the climbed window, its balconette and corbels, the window over the door with its shutters, the right corner's
  quoins, the eave and the gutter, and the climber lie on the video's at 45, 57, 72 and 84;
* the roof's edge and the climber at 93;
* the door at 10 and 25.

The tonal statistics (decision 0300) hold: the luminance's percentiles 0.76-1.06 of the video's, the fine detail 1.13,
the saturation 0.90. The whole shot beside the video, frame for frame: `reference160_sbs.mp4`. When the shot starts the
scene (`--demo_only=reference`) its first pose is held a second while the sky's and the bounce's light settle; its
first frames had been hazy. 38 of 38 animation gates.

## Decision

KEEP.

## Rejected / not done

* **The surroundings.** The video's house stands at a square's corner, with an arcaded palazzo across it and, from the
  roof, the city and its cathedral's dome low beyond. Ours closes a street off our square, between taller houses. The
  camera and the house agree with the video's; what is behind them does not.
* A scale chosen by the run alone (0.9) or the hang alone (0.78): one scale for the scene, as a real one has.
* The house's depth (6.5 m) and back: unseen in the video.
