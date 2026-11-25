# 0347 — The leap to the eave, the climb onto the roof, the walk up it, the head

Status: **KEEP** · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_traversal_3d.cpp`: with `leap_push_force`, an eave's lip out over the hold leapt to
  (`TRANSFER_JUMP_UP`, not `TRANSFER_REACH_UP`); with `climb_pull_speed`, up from a hang onto a ledge (`KIND_HIGH`) the
  clip played no faster than its rise at 1.25 times that speed, the hips let go of the arms' reach over the pull and
  the press, the feet on the wall found under the edge or hanging free, and the climb left at its own pace (not
  `exit_speed`);
* `showcase/scripts/rooftops_player.gd`: the head's look no steeper than 50° and turned into the wall as it steepens,
  from the head itself, at least 3 m out (`LOOK_UP_MAX`, `_look_way`, `_head_at`); the joint limits' neck bent sideways
  15° at most; roofs walked up to 28° (`CRAWL_SLOPE`), leaning into the slope (`ROOF_LEAN`), a walk on a roof starting and
  stopping at 4 m/s² (`WALK_ACCEL`); the first step off a climb at the ground's rates; `--head_look=0`;
* `showcase/scripts/rooftops_demo.gd`: up the roof walked to the ridge;
* `showcase/tools/anim_quality.gd`: `--dump=2` (the neck, the head's and the chest's tilt every frame).

## Problem

The owner, after milestone 204: "much nicer — only the first jump bends the head in a strange way, and I would fix the
last pull-up onto the roof"; then: "I meant the last jump onto the roof's outer edge, that is unnatural — and walking
on the roof needs improving too".

## What was found (milestone 204, the RTX's films and `--dump=2`)

* **The head**: caught on the wall after the jump at it, the chest rolled 35° off upright and the head, held upright,
  bent the neck 31° sideways; in the leaps the head looked straight up at the next hold, tipped back to the neck's
  limit, and swung 40-60° from side to side (a target over the head says nothing about which way across); before the
  eave, turned 84° one way and the other at up to 1,150 °/s.
* **To the eave** (the lip 0.5 m out from the wall over the window's lintel): reached for — the body drifted up and out
  from the wall to it with nothing pushing it, the legs hanging, an arm flung out.
* **Onto the roof**: the climb-up's clip (1.25 s) scaled onto 1.9 m: the hips rose at up to 3.3 m/s, held under the
  arms' reach and then let go (0.4 m in 0.15 s: 2.9 body weights, then −0.8); the feet braced at the edge's face — under
  an eave, on nothing; at its end the body set off up the roof at `exit_speed` (1.5 m/s) at once and lurched.
* **Up the roof**: on hands and feet (milestone 156's crawl, for roofs over 15°) at 0.65 m/s, the hips flexed past their
  range (1.8 % of the roof's frames).

## Change

* **The eave's lip leapt to** (with `leap_push_force`): a short dyno up and out from the wall (0.2 m of rise, 1.9 m/s off
  the push), the legs swinging under the lip at the catch.
* **The climb onto the roof** (with `climb_pull_speed`): no faster than 2.5 m/s (1.25 × `climb_pull_speed`; 1.7 s for the
  roof's 1.9 m), the hips let go over the pull and the press, the feet on the wall found under the eave, the body
  walking on from the climb at its own pace.
* **The head**: the look no steeper than 50° over the horizon, its way across turning into the wall as it steepens, from
  the head bone, at least 3 m out; the neck bent sideways 15° at most (the head goes with the chest's roll).
* **Up the roof walked** (21°; crawled only over 28°), the body leaning into the slope by 0.6 of its angle, starting and
  stopping at 4 m/s² on it.

## Measurements (headless; the demo from the house)

| | milestone 204 | **milestone 205** |
|---|---|---|
| climbing: the neck bent sideways at most; turned at most; its fastest turn | 32°; 81°; 1,152 °/s | **18°; 56°; 1,032 °/s** |
| the first leap: the head's turn | 58° → −12° → 41° (looking straight up) | **20° → −10°**, then 50° to the next hold across |
| to the eave | reached for (0.75 s) | **leapt to** (1.23 s: 1.9 m/s off the push) |
| onto the roof: time; the hips' fastest; support force | 1.24 s; 3.3 m/s; −0.8 to 2.9 body weights | **1.7 s; 2.5 m/s; −0.2 to 2.3** |
| `anim_quality.py`: the mantle's body jump; limbs in the stone | 189 m/s²; 4.7 % | **142 m/s²; 0 %** |
| up the roof | on hands and feet, 0.65 m/s; the hips past range 1.8 % | **walked**, 1.6 m/s; 0 % (toes on the tiles 2.4 %, 1.6 before) |
| `anim_quality.py` gates; `effort.py --profile arcade` | 38 / 45; 8 / 8 | 38 / 45; 8 / 8 (the pace 8.7 → 9.2 s) |

`climb_check.gd` (37 lanes) and `traversal_check.gd` (19) are identical to milestone 202's.

## Decision

KEEP: the eave's lip leapt to (with `leap_push_force`), the climb up from a hang paced, braced and left at its pace (with
`climb_pull_speed`) — off by default; the showcase's head look, neck, roof walk.

## Rejected / not done

* **A muscle-up's knee drive**: over the eave the legs still hang straight for a moment as the hips pass the edge (the
  mannequin's clip, keyed for a plain wall); a clip keyed for an eave, or Mixamo's (rejected in milestone 197: through the
  eave in half its frames), is the next step.
* **The crawl** stays for roofs over 28°; its own clip's hips are unchanged.
