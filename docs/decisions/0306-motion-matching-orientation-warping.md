# 0306 — Motion matching's orientation warping: the hips turned to the way it goes, the chest kept on its facing

Status: **KEEP**, on by default · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_motion_matcher_3d.{h,cpp}`: `orientation_warping`, `orientation_warping_max_angle`,
  the spine found from the hips;
* `benchmarks/project/scenes/matching_lab.gd`: `--matching_orient`, `--matching_orient_max`, `--matching_strafe`;
* `bench/matching_video.gd`: `--follow=walker`.

Checks: Matching Lab (16 characters, 60 s, CMU locomotion) walking as before and strafing; a strafing walker filmed
on lavapipe.

## Problem

A third-person game's character often faces one way and walks another: it aims at something while it walks round it.
Motion matching searches its database for a clip that goes the asked way while facing the asked way. Between the
database's few side and back steps (CMU subject 69's) the clip it finds goes some degrees off. The synchronisation
then drags the character the asked way, and its feet slide sideways ("moonwalking").

Matching Lab with every walker facing the square's centre while it walks to its waypoints (`--matching_strafe=1`;
runners face the way they run):

* the feet slid 0.158 m/s in contact, against 0.062 walking as before;
* the character trailed the simulation by 9.1 cm, against 4.3.

## Change

`orientation_warping` (as Unreal's orientation warping does it):

* The angle about the vertical from the clip's root motion to the simulation's velocity is measured. It is eased in
  with speed (0.3-0.7 m/s), held within `orientation_warping_max_angle` (90°) and eased (0.1 s).
* The hips are turned by it, which turns the legs, so the feet step the asked way.
* The spine (found from the hips: the child that leads to neither leg, then on while a bone has one child; CMU's
  LowerBack, Spine, Spine1) turns back by an equal share on each bone, about the vertical in its parent's frame. The
  chest, the arms and the head keep the facing.
* The root motion is turned with the hips, so the character goes where its feet go and the synchronisation has less
  to drag.
* The lean (decision 0305) is measured against the clip's own, unturned root motion: the warping turns the clip, it
  does not lean it.

It applies in the full updates (LOD 0), as the feet's IK does.

## Measurements

Matching Lab, 16 characters, 60 s:

| | foot slide (m/s) | locked | trajectory error (m) | lean gain | lean error | pop p99 (mm) | mean turn of the hips |
|---|---:|---:|---:|---:|---:|---:|---:|
| walking, milestone 164 | 0.062 | 0.036 | 0.043 | 0.67 | 2.75° | 37.8 | — |
| walking, orientation warping | 0.063 | 0.035 | **0.038** | **0.71** | 2.77° | 37.2 | 5.5° |
| strafing walkers, milestone 164 | 0.158 | 0.061 | 0.091 | 0.65 | 3.92° | 38.8 | — |
| strafing walkers, orientation warping | **0.127** | 0.054 | **0.077** | 0.69 | 3.64° | 38.8 | 11.5° |

* Walking, the character follows the simulation 12 % closer, and its feet slide as before.
* Strafing, the feet slide 20 % less and the trajectory error is 15 % lower.
* The largest angle (45°, 90° or 135°) makes no difference: the clips found go at most some 20° off. The rest of the
  strafe's slide is the database's: its side steps are few, and none at a jog.
* Turning the warp only on the hips, measured from the character's own velocity and without turning the root motion,
  had cut the strafe's slide to 0.129 m/s and left its trajectory error at 0.091 m.

## Decision

KEEP, on by default.

## Rejected / not done

* Larger angles: the database's clips are never that far off.
* Strafing at a run: its slide is the database's (no jogging side steps). A database with the eight directions at
  each pace is what AAA locomotion sets are made of; CMU's is not one.
* Physical animation (forces, hits, ragdolls blended in): the next milestone.
