# 0093 — body_test_motion() results kept within one move_and_slide()

Status: **KEEP**, on by default (milestone 17) · Branch `fellow/geometry` · Setting `rendering/fellow/physics/motion_query_cache`
· Code: `scene/3d/physics/physics_body_3d.*` (`FellowMotionCacheScope`, `_fellow_body_test_motion()`),
`scene/3d/physics/character_body_3d.cpp` (`move_and_slide()` opens the scope), `scene/3d/physics/collision_object_3d.*`
(`fellow_transform_sends`), `fellow_settings.*`.

## Problem

`CharacterBody3D::move_and_slide()` calls `PhysicsServer3D::body_test_motion()` several times per call; some calls
repeat an earlier one exactly: `_on_floor_if_snapped()` and `apply_floor_snap()` both test the same downward motion
from the same transform when the body is not on the floor.

## Change

Within one `move_and_slide()` nothing in the physics world changes but the body's own transform, and that only through
the node (`body_set_state(TRANSFORM)`, counted per node). A test with the same parameters (compared bit for bit:
transform, motion, margin, max collisions, flags, exclusions) from the same server-side transform (the same count of
transforms sent) returns the kept result. Up to 8 results per scope, stored compactly (the collisions used only).

## Verification

Physics Lab: the same per-tick hashes in every case, both engines.

## Results

On the Physics Lab terrain as it was (facing down, the characters sinking or falling through it, so never on the
floor): GodotPhysics3D 5.3 tests per character per tick, 58 % repeats; container `chars` 22.4 → 11.0 ms per tick,
laptop (`m17_f253`) 25.2 → 10.5 ms. **On the fixed terrain** (2026-10-25: the characters stand on it): 2.4 tests per
character per tick, 0.7 % repeats with GodotPhysics3D, none with Jolt; no measurable difference (container, three
runs: 17.54 against 17.51 ms with GodotPhysics3D, 7.87 against 8.20 with Jolt). Kept on: it costs nothing measurable
and halves the motion tests of characters in the air (jumping, falling), which is where the repeats are.
