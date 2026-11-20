# FellowEngine — milestone 160 report (the reference matchmoved: its camera, its house, its climber)

Status: **FINAL** (2026-11-20). Branch `fellow/geometry`. Everything ran in the cloud (CPU, lavapipe); the laptop's GPU
stays paused at the owner's request.

## 0. Result

The owner wants the reference shot to agree with the video "frame by frame, 100 %". Decision 0301:

* **The camera** is solved from the video's own background (structure from motion, outside the repository): 11 722
  tracked points, every one of the 96 frames registered, 0.50 px. It cranes straight up the house 2.1 m out from its
  wall. Decision 0297's camera, from the vanishing points and the fit's depth, had moved in along the wall and turned
  30-40° off on the roof.
* **The house** is measured through it, on its front rectified onto a metric grid, and rebuilt from those measures
  (`reference_performance.json`'s `"house"`, read by `casa_riferimento()`):
  * 6.64 m wide;
  * an arched door near the left corner, under a tall window with louvred shutters;
  * the climbed window 1.6 m from the right corner, its sill 2.01 m up, over a balconette on two corbels with a mask
    between them;
  * a bench;
  * the gutter 3.85 m up, the lip 0.48 m out; a 27° gable.
* **The climber** is fitted in the world through that camera: its holds on the replica's surfaces, gravity in the air,
  the scene's scale against the mannequin solved (one scale for the run and the hang).
* **The street** before the house is flagged as the video's, with no posts across it and no raised pavement.

## 1. Checks (cloud)

* The reference shot at the video's frames 10, 25, 45, 57, 72, 84 and 93, with the video's edges drawn over it and the
  two blended. The windows, the balconette, the corbels, the right corner, the eave and the gutter, the roof's edge
  and the climber lie on the video's.
* The baked clip's hips through the baked camera: within a few pixels of the fit's.
* The tonal statistics as decision 0300's: luminance 0.76-1.06 of the video's, fine detail 1.13, saturation 0.90.
* The whole shot rendered at 24 fps (Movie Maker, lavapipe) and set beside the video: `reference160_sbs.mp4`.
* `anim_quality.py`: 38 of 38 gates, none worse than milestone 158's baseline (the player, its clips and the
  traversal are unchanged; the street round the house lost its posts and raised pavement). The engine is unchanged.

## 2. Next

* **The surroundings**: the video's house stands at a square's corner, with an arcaded palazzo across and, from its
  roof, the city low beyond to the cathedral. Ours closes a street between taller houses.
* The game's own clips from the measured performance (the run, the leap at the wall, the balconette, the hang, the
  mantle, the scramble).
* The GPU, when the owner allows it.
