# 0338 — Mixamo's hangs and moves from hold to hold, fitted to the traversal

Status: **KEEP** · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_traversal_pose_3d.{h,cpp}`: `hand_flight_weight`, `hand_slide`, `_clear_wall()` (the
  knees and the balls of the feet kept off the stone the feet brace on);
* `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`: the pose hints' `wall` (the stone a braced foot is on, from
  `_hang_foot()`) and `slide` (hanging from one ledge);
* `showcase/tools/blender/mixamo_retarget.py`: the kinds `settle` (a still moment swayed) and `span` (a cut, the hips'
  sway kept), `anchored` with a span; the clips `hang_idle`, `hang_catch`, `hang_reach_l/r`, `hang_across_l/r`;
* `showcase/scripts/rooftops_player.gd`: the hangs and moves Mixamo's again (`MOCAP_KEEP_OWN` the fall and the free
  climb's loop), the catch (`hang_catch`, a state added to the player's copy of the tree), the shimmy at the body's pace
  (`ts_hang`; the traversal's `shimmy_speed` 0.6 m/s), `hand_flight_weight` 0.3 and `hand_slide` 1;
* the tools: `showcase/tools/climb_look.gd` (new: the climbing filmed, or recorded headless — the bend of every limb,
  the hands' and feet's distance to their targets), `showcase/tools/play_look.gd` (`no_pixel_history`).

## Problem

Milestone 195 (decision 0337) gave the hangs and the climb back to the mannequin's clips, Mixamo's having twitched at
rest, not been smooth and put more limbs through the walls. The owner: "then why did we bother with Mixamo if we go
back to our poor ones? We must polish the climbing and free-running animations to the max."

## What was found (headless, `climb_look.gd`: the run at CasaCrema, its first hold, 4 s still, along it each way; and
the demo's climb up its front)

* **The hang at rest**: Mixamo's "Braced Hang" (1.3 s) is a jump up into the hang and its settling, not a hang; decision
  0336 looped its last 60 % — a 0.585 s loop of the settling's bounce: the hips **17 cm up and down every 0.6 s**, at up
  to 2 m/s. The mannequin's own: 0.5 cm.
* **The moves**: the traversal put both hands on their arcs with the IK's full weight through every move — Mixamo's
  hops' arms never showed, their bodies did; the reaches and the moves across were the mannequin's own between Mixamo's
  hops (two styles, one after the other, every 0.7 s).
* **The shimmy**: Mixamo's at 0.45 m/s played whatever the body's pace (the traversal's 1 m/s), its hands held to the
  traversal's own stepping (a 0.3 m stride) — its body's rhythm and its hands' at odds.
* **Limbs through the walls** with Mixamo's: the knees (a captured braced hang has its knees forward, into the wall
  under the traversal's feet) and the balls of the feet — 8.3 % of the hanging frames, 7.7 % of the moves'.

## Change

* **The hang at rest** (`settle`): "Braced Hang" from 1.0 to 1.13 s — the settled moment — swayed to and back over four
  seconds (a cosine: still at either end); **the catch** (`span`, 0.47-1.3 s: from the hands on the ledge to settled)
  played when a hold is caught from the air.
* **Hand over hand** from Mixamo too: up — a half of "Climbing Up Wall" each (the left hand up with the right foot, then
  the right with the left), across — a step of the braced shimmies; each played over its move's time, the hips held
  (the traversal moves the body).
* **A hand between holds** (`FellowTraversalPose3D.hand_flight_weight`, the showcase's 0.3): off its hold, it is reached
  for by that share of the IK, the grip eased in and out over 0.1 s — the captured arm carries it, the IK puts it on the
  hold as it takes it. 1 (the default): as before.
* **The shimmy** at the body's pace (0.6 m/s; the clip's rate from the body's speed) and its **hands where the clip's
  are along the edge** (`hand_slide`, the showcase's 1; their height and depth the edge's): they stay where they took
  hold while the body moves, then step.
* **Knees and toes off the stone** (always, hanging and between holds; the traversal gives the stone the feet are braced
  on): a knee within 4 cm of it or behind it — the leg turned about the line from the hip to the ankle the least way
  that clears it, outward first (a climber's knee turns out; the foot stays); the ball of the foot behind it — the foot
  pitched about the ankle onto it. Whatever the feet's weights (a hop's legs swing free of the IK, not through the wall).

## Measurements

* Hanging still (the hang program, 3-6 s): the hips' range 17.2 → **1.8 cm** (vertical), their fastest 1.96 → 0.04 m/s.
* `anim_quality.py` (the climb up CasaCrema; against milestone 195's mannequin clips): **30 of 38 gates**, no
  regression; hanging's fastest joint 1,750 → **814°/s** (p95 1,567 → 615), from hold to hold p95 1,887 → **1,355°/s**
  (max 2,000 → 2,140; the gate 2,200); limbs through the walls hanging 2.1 → **0.0 %**, from hold to hold 2.6 →
  **1.2 %** (Mixamo's in milestone 194: 17.4 %); the hips' acceleration from hold to hold p95 215 → 447 m/s² (Mixamo's
  hops spring; the gate is the mantle's).
* `climb_check.gd`, `traversal_check.gd`: every lane as before.

## How it got there

* With the moves Mixamo's and the IK on the hands throughout (`hand_flight_weight` 1): hanging p95 814°/s, through the
  walls 14.6 % hanging, 9.8 % from hold to hold. 0.3: 615°/s, 8.3 %, 7.7 %. The knees and toes kept off the stone at the
  feet's weight: 4.2 %, 3.3 % (the knees gone); at full weight whatever the feet's: 0.0 %, 1.2 %.
* The shimmy at the body's pace but its hands on the traversal's stepping: the hips bobbed 13 cm, the hands slid at a
  stride of their own; with the clip's hands along the edge they hold, then step, and the hips 9 cm.

## Decision

KEEP: Mixamo's hangs and moves in the showcase; `hand_flight_weight` and `hand_slide` opt-in on FellowTraversalPose3D
(the defaults leave every other scene's climbing as it was), the knees and toes kept off the stone for every scene.

## Rejected / not done

* A captured hang's grip shifts looped (decision 0336's): its settling's bounce over and over.
* The mannequin's reaches between Mixamo's hops: two styles in turn.
* Open: the climb up onto the top from a hang, the run up a wall and the vault from Mixamo ("Braced Hang To Crouch",
  "Run To Start Climbing", the vault: root motion to move onto the root bone); the knee of the jump at a wall (8.8 % of
  the approach, 11 cm); the toes on the tiles (3.9 %).
