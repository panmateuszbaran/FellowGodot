# FellowEngine — milestone 91 report (occlusion through several things)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0199-occlusion-through-several.md`.

## 0. Result

* A sound behind two panes of glass is muffled as two (0.6), behind a pane and a wall as by the wall (1.0): up to three
  colliders on a path from the listener add their `fellow_occlusion`; a plain wall ends the path at once, as before.
* **Checked**: two new lanes on GodotPhysics3D and Jolt, the release template the same; decisions 0186 and 0198's
  lanes and the listened mix unchanged.

## 1. Development

`modules/fellow_world/fellow_acoustics_3d.cpp`; `benchmarks/project/bench/acoustics_check.gd` (two lanes).

## 2. Verification

Decision 0199's table. The night's final checks on this build: the full regression suite (35 scenes, the release
template against the editor build, FellowEngine off against on) exact; the editor check passes (34 icons). The laptop:
not run (not linked to the session).

## 3. Measurements

A ray more for each partial collider on a path; none for plain walls.

## 4. Next

Transmission by frequency, diffraction paths and portals.
