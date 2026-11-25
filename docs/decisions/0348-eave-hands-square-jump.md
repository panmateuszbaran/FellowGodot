# 0348 — The hands round the eave's lip; the body square to the wall at the first jump

Status: **KEEP** · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`: a leap (with `leap_push_force`) to a lip standing out over
  the hold left by `LIP_ROUND_OUT` (0.2 m) or more takes the hands round it — a curve out past its front and onto its
  top from outside (`LIP_ROUND_CONTROL` 0.5 m), from the push's start to the dead point, the IK theirs from a third of
  the way;
* `showcase/scripts/rooftops_player.gd`: the jump at a wall the mannequin's own clip again (`MOCAP_KEEP_OWN`), blended
  in over 0.45 s (`INERTIA_JUMP_WALL`); `--mocap_keep=`;
* `showcase/tools/effort.py`: a flight measured from the hips' fastest climb; `anim_quality.gd --dump=2`: the hips' and
  the chest's yaw and roll, the forearms through a surface.

## Problem

The owner, after milestone 205: "the leap to the eave still isn't nice — the hands go through the roof and are suddenly
on the edge; and why is the torso twisted at the start, in the first jump at the wall? What is it for?" It is for
nothing: a fault.

## What was found

* **The eave**: the lip is 0.24 m over the lintel's hold and 0.48 m out from it. The hands went to it in a straight line
  from below with a 0.15 m bulge, carried by the clip's arm reaching straight up (the IK 0.3 of the way until halfway):
  the forearms 6-15 cm into the eave's underside for 7 frames, then onto the lip at 8.2 m/s.
* **The twist**: the run's last stride turns the chest 37-42° from the hips (Mixamo's sprint); Mixamo's "Run To Start
  Climbing" (the jump at a wall, milestone 197) keeps that turn and reaches with one arm; caught by both hands on the
  edge, the chest rolled 34° and the hips 25° off level (`--dump=2`). With the mannequin's own jump at a wall (both
  arms up, the chest square) and Mixamo's catch: 9° and 2° — the twist is that clip's.

## Change

* The hands round the lip: a cubic curve from the hold left to the lip's — out first at the hold's height (the eave's
  underside is just over it), up past the lip's front, onto its top from above and outside (its controls 0.5 m out at
  the hold's height and 0.22 m out 0.12 m over the lip) — eased from the push's start to the dead point; the IK homes
  in from a third of the way (`hand_reach` 0.5 + 1.5 k). A quadratic first had brushed the eave's underside.
* The jump at a wall is the mannequin's own clip; its arms' swing up out of the sprint blended over 0.45 s (at 0.3 s an
  upper arm turned at 2,240 °/s, past the 2,200 gate).
* `effort.py`: a leap's flight from the hips' fastest climb after the hands and feet let go (the legs still pushing as a
  short leap's hands leave had read as flight: 0.57 g). Re-read, milestone 202's leaps decelerated at 1.32-1.38 g after
  their fastest point (braked in the air, not floated: milestone 203's 0.66 counted the push's end), milestone 204's at
  0.97-0.99 g (read 0.91-0.94).

## Measurements (headless; the demo from the house)

| | milestone 205 | **milestone 206** |
|---|---|---|
| to the eave: frames with a forearm through the eave; its depth | 7; 6-15 cm | **0** |
| to the eave: the hands' fastest | 8.2 m/s (onto the lip in a frame) | 5.7 m/s (out round the lip, eased) |
| caught on the wall: the chest's roll; the hips' | 34°; 25° | **9°; 2°** |
| the run and the jump at the wall: the fastest joint | 2,037 °/s | 2,175 °/s (gate 2,200) |
| the neck climbing: bent sideways; turned; its fastest turn | 18°; 56°; 1,032 °/s | 18°; 56°; **912 °/s** |
| `anim_quality.py`; `effort.py --profile arcade` | 38 / 45; 8 / 8 | 38 / 45; 8 / 8 (flights 0.97-0.99 g, the eave's 0.85) |

## Decision

KEEP: the hands round an overhanging lip (with `leap_push_force`: off by default); the showcase's jump at a wall the
mannequin's own; the effort tool's flight from the fastest climb.

## Rejected / not done

* Turning Mixamo's jump at a wall square (a counter-turn of the spine through the jump): the clip's one-armed reach
  would stay; the mannequin's clip is both arms, as the catch needs.
* The eave reached for by hand instead of leapt to (it is 0.24 m up): the owner asked for a jump; an arcade's leap with
  the hands round the lip.
