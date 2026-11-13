# 0213 — Vehicles dented where they hit

Status: **KEEP** (`FellowVehicleDamage3D`, a child of a `FellowVehicle3D`: nothing changes without it) · Branch
`fellow/geometry` · Code: `modules/fellow_world/fellow_vehicle_damage_3d.{h,cpp}` · Checks:
`benchmarks/project/bench/vehicle_check.gd` (two lanes, and the bumps lane's car undented).

## Problem

Decision 0185's cars drive, slide, jump and stop against walls without a mark: a car that hits a wall at 50 km/h
looks as it did. Damage that shows where a car was hit is common in the vehicle games of the genre (GTA's and
Forza's deformed panels, racing games' crumpled bonnets); the readiness assessment listed "no damage".

## Change

`FellowVehicleDamage3D`, a child of the vehicle:

* **Impacts from the body's contacts**: it turns the vehicle's contact reporting on (at least 8 contacts) and reads the
  contacts' impulses every physics step. When they exceed `impulse_threshold` (2 500 N·s in a step) the excess is
  shared among the contacts; each dents the body where it touches, `softness` (2·10⁻⁵ m per N·s) deep, along its
  normal into the body (the side towards the centre of mass). `apply_impact()` for the game's own (a blast).
* **The dent**: the vertices of the vehicle's meshes (every `MeshInstance3D` but the wheels', or `mesh_paths`) within
  `radius` (0.7 m) of the contact move by the depth × (1 − (d/r)²)², none more than `max_depth` (0.3 m) from where it
  was. The dented meshes are copies made at the first impact, their normals recomputed from the dented triangles; the
  originals are kept (`repair()`). A mesh with blend shapes is left as it is; the collision shapes are not changed.
* `get_damage()` (the deepest dent over `max_depth`, for the game's damage model), `get_deepest_dent()`, the signal
  `impacted(position, impulse, depth)`.

## Checks (cloud: the CPU)

`vehicle_check.gd`, the editor build, GodotPhysics3D: cars whose body is a box of 8 × 4 × 20 quads (824 vertices)
driven into a wall 30 m ahead:

| lane | expected | result |
|---|---|---|
| at 30 km/h | the front dented 5-30 cm into the body, the rear not; the wall stops the car | 29.4 km/h; four impacts over four steps (7 169, 6 135, 3 638, 2 543 N·s); the front 9.3 cm along +Z (into the body: share 1.0), the middle 5 mm (within the radius of the front edge), the rear 0; stopped at the wall |
| at 5 km/h | nothing dented | 4.9 km/h, no impact over the threshold (the steps' impulses up to about 1 700 N·s) |
| the bumps lane's car (ten 10 cm bumps at 40 km/h) | nothing dented | nothing; its suspension and pitch as before |

The other lanes the same; the editor check (37 icons). The laptop (job f409, a release template, MSVC): GodotPhysics3D
the same, value for value; Jolt: three impacts (7 357, 6 118, 3 044 N·s), the front 9.6 cm, the middle 1.4 mm, the
rear 0; at 5 km/h and over the bumps nothing.

## Cost

Only at impacts: the crash's four dents 0.31 ms together (the copies' rebuild included), nothing between them.

## Decision

Keep: a car shows where it was hit.

## Rejected / not done

* **A soft-body car** (BeamNG's node-and-beam bodies): the rigid body and its suspension stay; the dents are the
  visual meshes'.
* **Damage changing how the car drives** (an engine losing power, a bent wheel pulling): `get_damage()` and the
  signal give the game what it needs.
* **Parts coming off** (a bumper, a door): decision 0182's destructibles are static objects.
