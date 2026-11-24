# 0336 — Mixamo's hangs and climbs on the rooftops' mannequin

Status: **KEEP** (the showcase only; the clips are the owner's, not in the repository) · Branch `fellow/geometry` ·
Code: `showcase/tools/blender/mixamo_retarget.py` (the kinds `loop_tail`, `anchored`, `travel`; `HIPS_AT`; a loop's
height drift), `showcase/scripts/rooftops_player.gd` (`_variant_point()`: the shimmy's side; a hop's clip length).

## Problem

After decision 0335 (Mixamo's motion capture for the character's gaits, jumps and landings) the owner asked for the
second batch: the hangs and the climbs, and the start and stop clips that milestone 192's measurements called for (a
foot sliding 15-70 cm as the character starts or stops).

## Change

* **Downloaded** in the app's browser on the owner's account (13 more; `.tools\mixamo`): Braced Hang, Left and Right
  Braced Hang Shimmy, Braced Hang Hop Up, Left and Right, Braced Hang To Crouch, Climbing Up Wall, Run To Start
  Climbing, a vault with a hand planted, Start Walking, Stop Walking, Run To Stop.
* **Played** where the player's clips of those names are (`hang_idle`, `hang_shimmy`, `hang_leap_up`, `hang_leap_l`,
  `hang_leap_r`, `climb_move`):
  * the hang from Braced Hang's last 60 % (its first part catches the ledge), a loop with its seam crossfaded;
  * the shimmy by side — Mixamo's has a leading hand, the mannequin's was one clip for both ways: the hang's blend
    space's point swapped for the left or the right one by the stick's side (the model faces the body's −Z, its left the
    body's −X);
  * the hops with the hips held where they start (the traversal moves the body from hold to hold; the arms and legs
    reach about the hips as Mixamo's do), cut to their wind-up, flight and catch (20-75 % of Mixamo's clip: whole, played
    over the move's time, they ran three times as fast and the head turned at 2,330°/s), each played over the move's
    time from its own length;
  * the climb in place: a loop's drift taken out in height too.
* **The hips where the traversal expects them** (`HIPS_AT`): the hang's and climb's hips moved, with every joint, to the
  mannequin's own clips' (0.13 m out from the wall and 0.915 m up for the hangs; −0.08 and 0.885 for the climb):
  Mixamo's hang hung 0.13 m higher, its climb 0.77 m — hands the IK could not have put on the ledge.
* **Not played yet**: the start and stop clips, the climb-up from the hang, the run up a wall, the vault. Mixamo's start
  is a weight shift and three slow steps (2.9 s to 1 m/s), its stops three seconds from a walk and 0.9 s from 1.3 m/s —
  made for a cinematic, not a player's stick; `travel` records their speed for when the player follows a clip's own
  speed. The climb-up, run-up and vault are the traversal's root-motion clips (`FellowTraversal3D`'s `hang_clip`,
  `vault_clip`): Mixamo's hips carry the travel and need it moved onto the root bone first.
* The gaits as milestone 192's (a loop's height drift taken out of the climbs, not of the gaits).

## Measurements

`anim_quality.py` (headless): **30 of 38 gates**, as decision 0335's — every gate of the hangs, holds and climbs passes
with Mixamo's clips: the hang's fastest joint 1,200°/s (the mannequin's own: 1,750), from hold to hold 2,130°/s (2,000;
2,200 the gate), the knees never straight while hanging; the eight that fail are decision 0335's (the walk's and the
runners' joints against the gates' bands, the stop's 19 cm of slide, the jump at a wall's arms). The hangs' hips at
0.90-0.91 m, the climb's 0.88 m (the mannequin's 0.915 and 0.885).

## Decision

KEEP: the hangs and the climb from Mixamo; the starts and stops left to the mannequin's gaits' blend (the skating of
decision 0335 stands) until a start and stop of a player's pace — the foot lock (`FellowFootLock3D`) holding a
mocap foot through the blend is the next thing to look at: Mixamo's planted foot leaves more than the lock's
`unlock_radius` (0.3 m) of drag in a start.

## Rejected / not done

* Mixamo's hang at its own height: the hands 13 cm over the ledge, the climb's 77 cm.
* One shimmy for both sides (the mannequin's way): a left-handed lead going right.
* The hops whole: 2,330°/s at the head from hold to hold (the gate's 2,200).
