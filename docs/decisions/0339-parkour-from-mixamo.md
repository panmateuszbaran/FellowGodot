# 0339 — Free-running from Mixamo: the vault and the run up a wall; the RTX's check of the history per pixel

Status: **KEEP** · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_traversal_3d.cpp`: a mantle with a clip's root motion looks for its hold at the wall
  only up to the root's highest sample;
* `showcase/tools/blender/mixamo_retarget.py`: the kind `root` (the hips' travel the root's — less the change of the
  hips over the feet, or at the lower foot's height over the line from the take-off's to the landing's: `"root":
  "feet"`); the clips `vault`, `hang_climb` (not played), `jump_wall`;
* `showcase/scripts/rooftops_player.gd`: the traversal's `hang_clip` and `vault_clip` from the library where played
  (`MOCAP_KEEP_OWN`: the fall, the free climb's loop, the climb-up from a hang);
* `showcase/tools/climb_look.gd`: `--program=course` (a low wall vaulted at a sprint, a 3 m block caught and climbed
  onto from the hang; the square's props and people out of the way).

## Problem

The owner (decision 0338): the climbing and free-running animations polished to the max. After the hangs and the moves
between holds, the free-running: the vault, the run up a wall for the first hold and the climb up from a hang were
still the mannequin's clips (keyed by hand, milestones 72-156), and the jump at a wall out of Mixamo's sprint its
fastest joint at 2,720°/s (the gate 2,200).

## Change and measurements (the course and the climb up CasaCrema; films on the owner's RTX through the job queue —
the owner allowed the GPU again)

* **The vault**: Mixamo's from the hand's reach for the top to the touchdown (0.33-0.93 s), its root at the lower
  foot's height over the line between the take-off's and the landing's — on the ground at either end, over the top as
  the legs swing across. The body over the 0.95 m wall: up to 0.98 m and on 1.6 m in 0.6 s (the mannequin's vault:
  1.6 m in 0.57 s).
  * Whole (its 4.4 m of travel scaled onto the traversal's 1.4 m): the body at 1.3 m/s over the wall.
  * Its root at the hips less their place over the feet at the start (a stride in the air): the body 0.66 m under the
    ground at the landing.
  * **The engine**: a mantle with a clip's root motion is held off the wall until the feet are near the top (decision
    0337), the last such sample found from the clip's end — a vault's root comes down and steps on after its landing, a
    rise there held the body at the wall through the whole vault (it never crossed). Now only up to the root's highest.
* **The run up a wall** (`jump_wall`): Mixamo's "Run To Start Climbing" from its last stride to the hands reaching up
  with a foot on the wall (0.67-1.27 s), the hips held — the player's jump moves the body, the traversal catches the
  hold (its catch plays). Limbs through the wall in the approach 8.8 → 6.0 % of its frames; its fastest joint 2,717°/s
  (the arm swinging up out of the sprint: the gate still fails).
* **The climb up from a hang**: Mixamo's "Braced Hang To Crouch" with its root motion (the root under the hips by the
  hang's offset at the start, the crouch's at the end) — over CasaCrema's eave (a lip 0.5 m out, a roof's slope) the
  head, an arm and a shin went through it in half the mantle's frames (50.7 %; the mannequin's own: none); onto the
  course's plain block it climbs, then ends crouched and the stand-up is a blend. Kept the mannequin's.
* `anim_quality.py`: 30 of 38 gates, no regression (the mantle onto CasaCrema's roof as before: its root only rises);
  the regression suite's 40 scenes identical.

## The RTX's check of milestone 191's history per pixel (decision 0333)

The demo's run, 1920x1080, the character hidden, the flicker along the scene's motion vectors (`reproj_flicker.py`):
with the history per pixel **8.11 %** of the pixels more than 3 levels from the frame before (d1), without it 8.58 %
(the mean second difference 2.71 / 2.79). Milestone 190's engine (before it, job f552): 8.79 %.

## Decision

KEEP: Mixamo's vault and run up a wall; the hold found before the root's peak (FellowTraversal3D, every scene: a clip
whose root only rises is unchanged). The climb-up from a hang stays the mannequin's until one fits an eave and ends
standing.

## Rejected / not done

* Mixamo's vault whole; its root from the hips' offset.
* Mixamo's climb-up from a hang over an eave.
* Open: the arm's swing up out of the sprint at the jump at a wall (2,717°/s), the toes on the tiles.
