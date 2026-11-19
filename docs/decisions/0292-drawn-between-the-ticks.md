# 0292 — Drawn between the ticks: the climber's pose where the body is drawn; the showcase's camera no longer shaking

Status: **KEEP** (the pose: the same as before with physics interpolation off; the showcase: physics interpolation for
its player) · Branch `fellow/geometry` · Code: `modules/fellow_animation/fellow_traversal_pose_3d.cpp` (`_pose()`); the
showcase's player (`tools/build_rooftops.gd`, `scripts/rooftops.gd`, `scripts/rooftops_player.gd`,
`scripts/rooftops_demo.gd`) · Checks: the drawn steps of the camera and of the body frame by frame at 144 Hz, the
gripping hands' drawn place at 144 Hz, `bench/climb_check.gd` (37 lanes), the regression scenes.

## Problem

The owner, of milestone 153's showcase on the laptop: "while moving, the camera vibrates". The player's body is a
`CharacterBody3D` moved at the physics ticks (60 Hz); the camera follows it in the idle frames, at the display's rate.
At any other rate the body is drawn where the last tick left it: some frames see it move twice as far, some not at
all, and the camera, following, shakes with it. Measured (headless, `--fixed-fps 144`, running at a constant 4.4 m/s,
432 frames): the body drawn 0 to 7.3 cm further each frame (should be 3.06; standard deviation 3.6 cm), the camera 1.2
to 4.6 cm (1.55 cm).

Godot draws a body between its ticks with physics interpolation, but `FellowTraversalPose3D` placed the hands and
feet relative to the skeleton's `global_transform` — the last tick's, not where it is drawn: with interpolation on,
the gripping hands would have been drawn off their holds by the difference (measured on CasaCrema's climb at 144 Hz:
median 0.3 cm, 90th percentile 2.8 cm, at most 7 cm).

## Change

* **`FellowTraversalPose3D`**: the targets (and the sway's spring, and `hang_fit`'s move) relative to the skeleton's
  interpolated transform (`get_global_transform_interpolated()`): where it is drawn; with physics interpolation off,
  the same transform as before.
* **The showcase**: physics interpolation on while the rooftops scene runs (`rooftops.gd` turns it on and back off),
  for the player alone (its scene's root opts out: the crowd, the birds and the camera move in the idle frames and are
  drawn where they are); the player's `AnimationTree` advanced in the idle frames (at the ticks the limbs moved in
  60 Hz steps under a body drawn between them), the physics ticks only for the way out of the hay (its root motion,
  read at the ticks); a placed body's interpolation reset (the demo's cuts, the hay).

## Checks

* **The drawn steps** (as above): the body and the camera 3.06 cm every frame (standard deviation 0.00 cm; before
  3.6 and 1.55 cm).
* **The gripping hands at 144 Hz**: on their holds where they were at 60 Hz (median 0.9 cm; the 90th percentile is
  the 0.1-0.3 s after a hand takes a higher hold, decision 0291).
* **`climb_check.gd`**: the 37 lanes value for value on GodotPhysics3D and Jolt (no interpolation there: the same
  transform). **Regression**: `climb`, `ledge_corners`, `traversal` exact.

## Decision

KEEP.

## Rejected / not done

* **The camera moved at the physics ticks**: still in steps against everything drawn in the idle frames, smooth only
  at 60 Hz.
* **Interpolation for the whole scene**: the crowd and the birds are moved in the idle frames; interpolated, they
  would be drawn a tick behind and stepping.
