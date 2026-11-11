# FellowEngine — milestone 72 report (mantling and vaulting)

Status: **FINAL** (2026-11-11): the owner showed a Godot project's mantling ("czy możemy podobny system wdrożyć u
nas?"), asked for it procedural with places for clips ("Zrób proceduralnie z miejscem na klipy"), first without the
GPU, then allowed it. Branch `fellow/geometry`. Decision record: `docs/decisions/0180-traversal-mantling.md`.

## 0. Result

* **`FellowTraversal3D`**: a character's body finds ledges in its way with the physics server's queries — a wall in
  reach facing the way it goes, a top it can stand on within the heights (0.5-2.3 m; 0.5 m more in the air), room for
  its capsule on the top, above it and over the edge — and climbs them: a low mantle (to 1.3 m), a high one (pulled up
  from a hang), or a vault over a thin obstacle (with no room on it, or running). The body moves along a path built
  from that room: its capsule never enters the world. Moving platforms carry the ledge; ledges are grabbed in the air;
  a beam the body could walk under is left alone; a ceiling bounds the search.
* **`FellowTraversalPose3D`**: the character's pose over its own animation — the hands on the edge, the hips hanging
  until the arms reach it, the feet on the wall's face and stepping onto the top, the torso leaning in (two-bone IK
  within 0.05 cm of every target in reach) — or a clip per kind (low, high, vault) played across the mantle, its root
  motion warped onto the ledge as the body's path.
* **Checked** on a course of nineteen obstacles with GodotPhysics3D and Jolt (the same decisions, the bodies ending
  within 2 cm of each other), in pictures and a video on lavapipe, in the regression suite and the editor; on the laptop (jobs
  f390, f391): the same course, 200 characters mantling and vaulting for 0.32 ms of the CPU a frame with Jolt (0.49 ms
  with GodotPhysics3D), the mantles filmed on the RTX.

## 1. Development

* `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`: the checks (`find_ledge()`, the automatic one each physics
  frame, `try_mantle()`), the path (procedural; a clip's root motion warped), the moving collider, the pose's targets
  (`get_pose_targets()`), the counters (`get_global_stats()`), `debug_draw`, configuration warnings.
* `modules/fellow_animation/fellow_traversal_pose_3d.{h,cpp}`: bones found by name in common rigs (`bone_names` for
  others); lean, hips, two-bone IK for the feet and hands; a clip's pose with its root's motion out; blending.
* `modules/fellow_animation/register_types.cpp`, `config.py`: the classes; the monitor "FellowEngine/Traversal time".
  `doc_classes/FellowTraversal3D.xml`, `FellowTraversalPose3D.xml`; icons.
* `modules/fellow_editor/fellow_editor_plugin.{h,cpp}`: the gizmo (the heights searched in front of the body) and the
  FellowEngine menu's "Add Mantling Character".
* `benchmarks/project/bench/traversal_check.gd`: the course (pictures, frames, the window's grid for a movie) and
  `--crowd=N`. `tools/fellow_regression`: the scene `traversal`. The editor check: the gizmo, the icons, the setup, the
  monitor.

## 2. Verification

* The course (`traversal_check.gd`, nineteen lanes) on the editor build: every lane as expected with GodotPhysics3D
  and with Jolt (a separate project); no frame of any mantle with the capsule in the world; the hands and feet on
  their targets; the clip's lane in the clip's time with its spine at the clip's angle and the root motion out.
* Lavapipe: the course's pictures (six mantles at six moments) and a video; the regression scene `traversal` (three
  mantles held partway) blessed and exact on two more runs; the full suite (editor and release template).
* The editor check: the traversal's gizmo and both icons, "Add Mantling Character" without a warning and undone.
* The laptop (release template): f390 — the course with both engines, crowds of 50 and 200 twice each, the mantles
  filmed on the RTX; f391 — the film again with the views' names in their cells.

## 3. Measurements

See decision 0180. The laptop (f390, release template, headless: the CPU):

| | 50 runners, GodotPhysics3D | 50, Jolt | 200, GodotPhysics3D | 200, Jolt |
|---|---|---|---|---|
| mantles and vaults a second | 22 | 22 | 89 | 89 |
| the traversals' time a frame | 0.12 ms | 0.08 ms | 0.47-0.50 ms | 0.32-0.33 ms |
| the frame | 0.70 ms | 0.50 ms | 2.46-2.59 ms | 1.81-1.89 ms |

`find_ledge()` 2.6-12 µs with Jolt, 4.5-28 µs with GodotPhysics3D; `check_interval` (2) halved the checks of a crowd
(0.46 → 0.25 ms a frame for 200 on the cloud).

## 4. Next

Hanging and shimmying along a ledge, climbing walls and ladders (the same checks and targets); a clip set made for the
mantles, with the hands' contacts marked for IK on the edge.
