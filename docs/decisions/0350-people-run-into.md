# 0350 — The square's people solid: run into, staggered, knocked down and up again

Status: **KEEP** · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_physical_animation_3d.{h,cpp}`: `get_up_from_animation(blend)` — lying, the bodies
  blended into the animation the game plays (its own get-up clip) without a motion matcher; a fall from `knock_down()`
  and a get-up go on with `balance` off too; a game rig's root bone on the ground gets no body;
* `showcase/scripts/rooftops_crowd.gd`: each person a capsule (`LAYER_PEOPLE`), `bump()`, the reactions (a shove, a
  stagger, knocked down and up again, back to their way or place);
* `showcase/scripts/rooftops_player.gd`: the people in the body's mask, its rays on the world alone (`RAY_MASK`),
  `_bump_people()`;
* `showcase/tools/blender/mannequin_anims.py`: `clip_stagger` ("stagger_back", "stagger_fwd"), `clip_get_up_back`,
  `clip_get_up_front`;
* `showcase/tools/build_rooftops.gd`, `rooftops.tscn`, `player.tscn`: the player's mask; a group and two routes moved
  off CasaCrema's run and climb;
* `showcase/tools/climb_look.gd`: `--program=bump` (`--bump=walk|run|sprint`, `--cam=npc`, `--bump_trace=1`).

## Problem

The owner: "I'd like more interaction with the NPCs: to run into them, knock someone over, not to pass through them."
The square's 46 people were figures moved along their ways; the player went through them.

## Change

* **Solid**: each person a capsule (0.27 m by 1.72 m; sitting, a shorter one over the bench) on its own layer, which the
  player's body meets and its rays (the edge, the wall ahead, the ground) do not see — a person ahead is no wall to
  jump at.
* **Run into** (the player's slides against a person, by how fast the body met them along the contact):
  * under 1.8 m/s (a walk): a shove — they give a little way (at most 0.35 m, once every 0.5 s while leant on), then
    stand back;
  * under 5.2 m/s (a run): a stagger — "stagger_back" from the front, "stagger_fwd" from behind (two quick steps the
    way of the push, 0.55 m, the trunk thrown, the arms out; its root motion moves the figure), turned square to the
    push; the body keeps 45 % of its speed;
  * faster (a sprint), or landed on from above: knocked down — the skeleton's bodies (`FellowPhysicalAnimation3D`,
    built for it and cleared after) fall about the feet and go limp, the ragdoll lies, and 1.1 s after it is still the
    figure is placed where it lies (its hips, the way its head lies) and plays "get_up_back" or "get_up_front" (on a
    side by the way the chest turns), the bodies blended into it over 0.8 s (`get_up_from_animation`); the body keeps
    70 % of its speed and goes on through.
  * Then a walker walks on, back into its way as it goes; the others walk back to their place (waiting while the
    player stands in it) and turn back to what they were doing. The people sitting are only shoved.
* **The engine**: `FellowPhysicalAnimation3D.get_up_from_animation(blend)` — milestone 183's get-up needs a motion
  matcher's database; a game with its own clips now places its character where the body lies, plays its clip and
  hands the bodies to it. `knock_down()` goes on to lying with `balance` off (it had stayed falling). A root bone on
  the ground under the hips (a game rig's, carrying the motion: the mannequin's "Root") gets no body: its capsule from
  the ground up to the hips stood in the ground and, pinned to the hips, pushed them up as they got up from lying while
  their drive pulled them down — the hips' body at 10.5 m/s.
* The crowd off the demo's run at CasaCrema and its climb (x 13.1 from z −7): the group at (12.6, −9.6) to (10.6, −8.6),
  the round of the square and the way across from its corner kept to the south of z −5.5 there.

## Measurements (headless, `climb_look.gd --program=bump`: one of the three talking in the square's middle, run at from
behind, from 6 m)

| | walk (1.6 m/s) | run (4.4 m/s) | sprint (7 m/s) |
|---|---|---|---|
| what follows | a shove: 0.1-0.35 m, the player stopped | a stagger forward, 0.55 m in 1 s; waits for the player to clear, walks back | knocked down: falls 0.75 s (limp at 61-65°), still 0.7 s later; up 1.1 s after, standing 2.0 s later; back talking 5.5 s after the hit |
| the player's speed on | stopped (leaning on them) | 1.98 m/s | 4.9 m/s (the other two of the group staggered and knocked down too) |

| getting up (sprint) | the root's capsule (before) | **no root body** |
|---|---|---|
| the clip's first pose from where the bodies lie (mean, by mass) | 0.40 m | 0.28 m |
| the bodies' fastest while blending | 10.5 m/s (the hips: pushed up by the root's capsule in the ground) | **4.4 m/s** (the feet drawn up) |
| lying: the bodies' mean speed (Godot Physics; Jolt) | 1.1 cm/s; 0.25 cm/s | 1.8 cm/s |

* The demo with the people solid (the whole scene, headless): no one run into, the body's path identical to the
  scene without people. `effort.py --profile arcade` 8 / 8; `anim_quality.py` 38 / 45.
* `balance_check.gd`, `physical_check.gd` (Jolt; CMU's root is its hips): identical but for the timings.
  `climb_check`, `traversal_check` identical.

## Decision

KEEP: `get_up_from_animation`, `knock_down` without balance, no body for a ground root; the showcase's people solid
and reacting.

## Rejected / not done

* The showcase on Jolt Physics (the ragdoll's joints steadier: lying at 0.25 cm/s against 1.1-1.8): the demo's climb
  differs by up to 9 cm and the trailing forearm across between holds (already at the pose's 2,000 °/s cap, the elbow
  hinge's correction on top) reached 2,471 °/s, past the gate. Godot Physics' ragdoll lies still enough to see none
  of it; the forearm is left for a milestone of its own.
* Every person's bodies built at start (1,000 rigid bodies standing still): built on a knock, cleared once standing.
* The player knocked down too: an arcade hero goes on, slowed.
