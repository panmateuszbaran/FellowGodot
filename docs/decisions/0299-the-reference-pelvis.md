# 0299 — The reference's pelvis: the fit held to a body's reach, the mannequin's briefs as the video's

Status: **KEEP** (the showcase's: its data and its mannequin; nothing in the engine) · Branch `fellow/geometry` · Code:
`showcase/tools/blender/reference_performance.json` (refitted), `showcase/tools/blender/mannequin.py` (`TORSO`'s
pelvis, `pelvis_shape`, the thighs, the dark core) · Checks: the spine's bend and the hips' flexion per frame, fit by fit;
the fitted mannequin over the video's frames; the reference shot beside the video; the 38 animation gates.

## Problem

The owner, of the reference shot:

* "why does our model's backside stick out so?"
* then: "I think there's a problem with the pelvis's deformation, check it — remember I want 100 % agreement frame by
  frame".

Two causes.

* **The pose.** Decision 0297's fit had nothing to say how far the pelvis tips forward or back. MediaPipe gives the
  pelvis two landmarks, on its sideways line. Only the spine's base, 13.5 cm above the hips, moved with that tip; the
  thighs' turns are their own. The fit drifted:
  * at the balconette (frames 50-60), the pelvis tipped forward 36° against the trunk;
  * on the tiles (84-96), 45-52°, with the thighs flexed up to 168° from it (a body reaches about 125°).

  From behind the pelvis showed its underside, the backside thrust out, the waist folded.
* **The shape.** The mannequin's pelvis, made in milestone 156, was:
  * 2-3 cm deeper behind than the video's beside its silhouettes;
  * sculpted buttocks on top of that;
  * briefs cut low behind over the thighs' tops.

## Change

* **The fit** (in the cloud, outside the repository, as decision 0297's) now has the body's ranges as residuals:
  * the spine's bend, the trunk in the pelvis's axes: held near 5° forward (σ 15°), arched back no more than 12°, bent
    forward no more than 60°, to the side 25°, twisted 40°;
  * each hip flexed no more than 125° and extended no more than 25°.

  The landmarks' error is as before: the main joints 7.31 px on average, against 7.23. The path, the camera and the
  holds were solved again from it; the holds moved by 1-2 cm (on the tiles up to 7), the hips by 1.4 cm on average.
* **The mannequin**:
  * the pelvis is as deep behind as in front: its back 1.2-1.7 cm shallower, up to 7 mm narrower;
  * the buttocks' sculpt is 0.8 cm (2.2 cm before);
  * the briefs' lower edge rises 3 cm behind, to the buttocks' crease;
  * the thighs' backs are 0.5-1.1 cm slimmer;
  * the dark core between the thighs is smaller.

## Checks

| | before | after |
|---|---|---|
| the spine's bend over the 96 frames | -52° … +39° | -7° … +21° |
| the hips' flexion, most | 169° | 129° |
| the main joints' error | 7.23 px | 7.31 px |

* The fitted mannequin over the video's frames 25, 29, 53, 57, 89 and 93: on the tiles the trunk and pelvis now lie on
  the video's (before, a second outline behind).
* The reference shot beside the video at frames 25, 53, 57, 89, 93: the backside no longer stands out; on the tiles
  the body is crouched as the video's.
* 38 of 38 animation gates; no regression against the baseline.

## Decision

KEEP.

## Rejected / not done

* A pelvis landmark of our own (from the silhouette's back line): the ranges alone settle it.
* **The camera** (found while checking the frames): decision 0297's camera came from the vanishing points and the
  fit's depth, and is least sure where the roof hides the walls. A camera solved from the video's own background
  (structure from motion: 11 700 points tracked over the 96 frames, all frames registered, 0.5 px) has:
  * the camera not moving in along the wall but craning straight up from 1 m to 5 m;
  * on the roof, a turn up to 30-40° off the old one;
  * the house taller than our replica (the balconette's top about 2.3 m up, the eave about 5 m).

  Next: the replica and the performance measured again through it.
