# 0216 — Parts that come off

Status: **KEEP** (`FellowVehicleDamage3D.detachable_paths`, empty by default: nothing comes off) · Branch
`fellow/geometry` · Code: `modules/fellow_world/fellow_vehicle_damage_3d.{h,cpp}` · Checks:
`benchmarks/project/bench/vehicle_check.gd` (the lane `bumper`).

## Problem

Decisions 0213 and 0215 dent a car's body and bend its running gear, but nothing comes off it: a bumper hit at 30 km/h
stays on, crumpled. Parts falling away are the most legible damage in the genre's games (GTA's bumpers and doors,
Burnout's and BeamNG's debris); the readiness assessment listed "nothing coming off".

## Change

On `FellowVehicleDamage3D`:

* **`detachable_paths`**: the parts that can come off (any `Node3D` of the vehicle: a bumper, a door, a mirror).
* **How deep a part is dented**: each impact's depth times decision 0215's reach, measured to the part's point nearest
  the impact (its bounds' box: a bumper's end is reached by a hit at its corner), added up over impacts.
* **`detach_depth`** (0.12 m): at it the part comes off — after the physics step (bodies are not added in it): a
  `RigidBody3D` of **`detach_mass`** (8 kg) where it is, a box of the part's bounds, on the vehicle's collision layers
  but not colliding with the vehicle (it starts inside it), moving as the vehicle's body moved at that point; the part
  becomes its child (a dented mesh keeps its dents). The signal `part_detached(part, body)`, `is_part_detached()`.
* **`repair()`** puts the parts back where they were (their transforms and places among their parents' children) and
  frees their bodies.

## Checks (cloud: the CPU; the editor build and the release template the same)

`vehicle_check.gd`'s lane `bumper`: a car with a bumper (a 1.86 × 0.22 × 0.14 m box at its front, `detach_depth`
0.1 m), hit lightly at 0.5 s (`apply_impact()` 4 cm deep), then into a wall at 30 km/h (decision 0213's crash), backed
off 3 m, repaired at 24 s:

| expected | result |
|---|---|
| kept after the light hit (and 0.5 s later) | kept |
| off in the crash, a body of 8 kg | off (the signal with `BumperDetached`, 8 kg) |
| at 23.9 s on the ground between the wall's face (z −29.5) and the car backed off, still | y 0.11 (half its height), z −29.46, the car's front at −24.97; 0.000 m/s |
| after `repair()` on the car where it was, its body freed | on the car, 0 m off, no body left |

The other 26 lanes the same values as before.

## Cost

At a part's coming off: one body and its shape (a node of the scene); nothing between impacts.

## Decision

Keep: hit hard enough, a part falls off and lies where it fell.

## Rejected / not done

* **The vehicle's collision shapes following its parts** (a car without its bumper shorter): the shapes are not
  changed (decision 0213).
* **Hinged parts** (a door swinging open before it comes off, a bonnet flapping): off or on.
* **A part's own convex shape**: a box of its bounds.
