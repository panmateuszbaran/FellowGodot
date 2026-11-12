# 0183 — Hanging from ledges, moving along them, and ladders

Status: **KEEP** (behind properties of `FellowTraversal3D` and a new node; a project's mantles and vaults decide as
before) · Branch `fellow/geometry` · Code: `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`,
`modules/fellow_animation/fellow_ladder_3d.{h,cpp}` (`FellowLadder3D`), `modules/fellow_editor/fellow_editor_plugin.{h,cpp}`
(the ladder's gizmo, "Add Ladder") · Checks: `benchmarks/project/bench/climb_check.gd` (six lanes),
`bench/traversal_check.gd` (milestone 72's course, unchanged), the regression scene `climb`, the editor check; the laptop
(job f394).

## Problem

Decision 0180 gave characters mantles and vaults; the readiness assessment's characters row still said "no climbing,
hanging along ledges or ladders" — the rest of a third-person game's traversal set: a ledge too high or with no room
on top is hung from and moved along, a ladder is climbed.

## Change

**Hanging** (`FellowTraversal3D`, action `KIND_HANG`): in the air, the ledge check of decision 0180 that finds a ledge
the body cannot climb (no room on its top: a wall rising behind it, an overhang) — or, with `hang_first`, one it could
— hangs from it when its edge is about `hang_reach` (2.05 m) over the feet (from 0.8 m less to 0.4 m more), if the
capsule fits there (`hang`; `try_hang()` from a controller). The body hangs below the edge facing the wall, `radius +
0.08` m off its face, blended in over 0.2 s. Each physics frame:

* **Along the edge** (the intent's component along it, `shimmy_speed` 1 m/s, eased): the ledge found again a step
  along — a ray into the wall just under the top, a ray down onto the top just inside the face — and half the body's
  width further; the body moves only if both are there and its capsule fits at the new place. It follows the edge as it
  turns (the face up to 50° from the last) and steps (0.3 m), and stops where the ledge ends, at a gap, or at something
  in the way. A moving collider carries the ledge.
* **Up**: pushing at the wall again (the push that jumped at it does not count), the intent up, or `climb_up()` — a
  ledge check from the hang; with room on the top, a high mantle from there (decision 0180's path and pose).
* **Down**: the intent away from the wall or down, or `release()` — the body falls, pushed 0.6 m/s off the wall, and
  catches no ledge for 0.6 s.
* **The pose**: the hands on the edge, stepping along it with the body (a 0.3 m stride), the feet on the wall's face.

**Ladders** (`FellowLadder3D`, a marker: its foot at the node, `height` up its Y, `width` along X, climbed from +Z;
`rung_spacing`, `rung_offset`; it draws nothing, the editor draws its rails, rungs and an arrow; a list of those in
the tree is all the traversals search). With `ladders` on, a traversal takes one when the body goes towards it in front
of it (within `ladder_reach` 0.6 m past the capsule, its feet along it), or walks off the floor at its top over it
(onto it backwards: out over the edge at the floor's height, then down, the capsule clear of the edge); or on
`try_ladder()`. On it the body faces it `radius + 0.12` m off, climbs at `ladder_speed` (1.2 m/s) as asked
(`set_climb_input()`, or the intent: up or towards the ladder climbs), steps off at its foot going down, and at its
top going up mantles off onto what is there (`exit_at_top`; a ledge check from the ladder). The pose: the hands and feet
on the rungs, a rung apart, changing over as it climbs.

Signals `hang_started`, `hang_finished(climbed)`, `ladder_started`, `ladder_finished(at_top)`; `is_hanging()`,
`is_on_ladder()`, `get_ladder()`; the global statistics count hangs and ladders. The FellowEngine menu's "Add Ladder".

## Checks (cloud: the CPU, lavapipe)

`bench/climb_check.gd`, the editor build, 9 s at 60 fps, a capsule 0.35 × 1.8 m with a test mannequin, each lane a
program of the controller's wishes; GodotPhysics3D and Jolt alike:

| lane | what is checked | result |
|---|---|---|
| a jump at a 2.5 m ledge with a wall rising 0.4 m behind it, then left, then down | hangs, moves to the end, lets go | hung, 1.35 m along it, stopped 0.21 m inside its end, fell to the ground |
| a jump at a 2.8 m ledge with room on top, `hang_first` | hangs; climbs on a new push | hung 1.5 s, a high mantle, on the top (2.79 m) |
| a jump at a round tower (radius 1.6 m) with a narrower one rising from it, then round it | follows the rim | 2.65 m along, turned 120°, 2.03 m from the axis all the way (1.6 + 0.35 + 0.08) |
| a 2.5 m ledge broken by a 0.8 m gap | stops at the gap | 0.21 m before it |
| a ladder against a 4 m platform, walked at | climbs, mantles off at the top | on the ladder 2.75 s, a low mantle onto the top (4.00 m) |
| the same from the platform's top, walked off over it | onto it, down, off at its foot | on the ladder 2.9 s, standing at its foot, walked away |

No lane's capsule overlapped the world in any frame (the first version moved the body from the platform's top straight
to the ladder's rung, through the edge: 13 frames). On the ladders the hands and feet reached their rungs exactly (190
samples a lane at full weight); hanging, the test mannequin's arms are 6 cm short of `hang_reach` and point straight up
at the edge. Milestone 72's course (`traversal_check.gd`, nineteen lanes) decides and ends as before.

A traversal's step (a mantle, a hang with its probes, a ladder): 1.6-1.8 µs on average on the cloud (editor build).

Lavapipe: the lanes filmed (the hang and the move along the ledge, the climb from the hang, the tower's rim, up and down
the ladders). The regression scene `climb` (a body hanging and moving along a ledge, one on a ladder, held at a frame):
the same picture on every run.

## Results on the laptop (job f394, release template at 8549eb5)

The six lanes with GodotPhysics3D and with Jolt: the same events, the same ends and distances as on the cloud (1.35 m
along the ledge, 0.21 m before the gap, 120.5° round the tower, 2.03 m from its axis), no frame of the capsule in the
world. A traversal's step while hanging or climbing: 1.9 µs with Jolt, 2.4 µs with GodotPhysics3D on average (its probes
are the physics server's rays and shape queries). The lanes filmed on the RTX (Jolt).

## Decision

Keep. The hang and the ladder reuse the mantle's checks, path and pose (a hang climbs by a mantle, a ladder's top is a
mantle), so they add no other way of moving the body; ladders are markers, not geometry the queries must recognise.

## Rejected / not done

* **Corners**: an outer corner (the ledge turning 90° away) and an inner one stop the body; it does not wrap round
  them (turns up to 50° a step are followed, so round walls are).
* **Jumping from a hang to another ledge**, wall climbing on free surfaces, ladders found from geometry without a
  marker, clips for hanging and ladders (the pose is procedural; the mantle from them can play its clip).
