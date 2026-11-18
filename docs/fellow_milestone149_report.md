# FellowEngine — milestone 149 report (the head following the gaze; a jump round a corner; hair shadowed by lamps)

Status: **FINAL** (2026-11-18). Branch `fellow/geometry`. The CPU and lavapipe in the cloud; the GPU still paused by the
owner (`f510`, `f511` wait in `.tools/wip`).

## 0. Result

* **The head turned towards what the face looks at** (decision 0278): `FellowHeadMotion3D.follow_gaze` (opt-in) —
  past a 10° dead zone 0.7 of the angle to the face's look target, at most 70° to the side and 35° up or down, eased
  over 0.3 s; the face told (`FellowFaceAnimator.set_head_turn()`), its eyes turning the rest. A target 60° to the
  left: the head 34.9°, the eyes 25.1° (without it the eyes stop at their 30° and the head stays); 30° up: 14° and 16°;
  150° round: the head at its 70°, the eyes at their 30°.
* **A jump round a corner from a hang** (decision 0279): `hang_jump()` with the wish along and no edge along it finds a
  ledge on the side of the wall past the corner (0.3 m to `hang_jump_distance` back, on it or standing out of it); the
  body goes round the corner in the air, turning a quarter, the wish held going on along the new edge — turned 90.0°,
  hanging 0.43 m off the side ledge (`TRANSFER_JUMP_CORNER`).
* **Hair shadowing itself from a lamp** (decision 0280): `FellowHair3D.self_shadow_light` may be an omni or spot light
  — the deep opacity map along the direction towards it from the hair's centre, the material dimming that light: a
  lamp where the sun was gives the same map (mean share 0.270) and dims the hair as the sun did.
* **Unchanged**: every earlier lane of `face_check.gd`, `climb_check.gd` (24 lanes value for value) and `hair_check.gd`;
  the regression scenes `face`, `climb`, `ledge_corners` and `hair` exact.

## 1. Checks (cloud)

* `face_check.gd` (`head_gaze`: two heads, one following, the target at four places); the regression scene `face`.
* `climb_check.gd` on GodotPhysics3D and Jolt (`hang_jump_corner`; the other lanes compared with the run before);
  the regression scenes `climb` and `ledge_corners`.
* `hair_check.gd` on lavapipe with the sun and with lamps 3 m and 1 m away, the self-shadowing on and off (the maps'
  shares, the pictures' luminance); the regression scene `hair`.
* The class reference from the doctool (`FellowHeadMotion3D`, `FellowFaceAnimator`, `FellowTraversal3D`,
  `FellowHair3D`), `make_rst.py` without a warning.

## 2. Next

On the GPU when the owner allows it: `f510` (the hang, 40 runs with the richer checkpoints) and `f511` (decision 0272's
A/B). On the CPU: more of the readiness assessment's gaps.
