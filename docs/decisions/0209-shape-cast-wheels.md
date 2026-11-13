# 0209 — Wheels that meet the ground with their shape

Status: **KEEP** (`FellowWheel3D.contact_shape`: ray by default; sphere or cylinder per wheel) · Branch
`fellow/geometry` · Code: `modules/fellow_world/fellow_vehicle_3d.{h,cpp}` · Checks:
`benchmarks/project/bench/vehicle_check.gd` (four lanes and their comparison; `--contact` with `--many`).

## Problem

Decision 0185's wheel finds the ground with one ray down its suspension: it stands on whatever is under its centre.
Driving at a curb, the tyre's front passes into the curb's face until the centre is over its edge, then the suspension
jumps by the curb's height in one step (a jolt, a tyre visibly inside the kerb); over a gap narrower than the tyre the
ray drops into it and the wheel with it. Shape-cast wheels are what current vehicle physics does for the player's car
(Unreal's Chaos vehicles sweep a sphere, Unity's assets sweep spheres or cylinders); the readiness assessment listed
"vehicles on rays only (no shape-cast wheels)".

## Change

`contact_shape` on each wheel: `CONTACT_RAY` (decision 0185's, the default), `CONTACT_SPHERE` (a sphere of the tyre's
radius) or `CONTACT_CYLINDER` (a cylinder of its radius and `width`, 0.22 m, along the steered axle). The shape is swept
down the suspension from fully compressed (its centre at the mount); where it first touches is where the wheel stands
(the compression the rest of the suspension, tyre and engine model take as before). Where the ray under the centre finds
the same height the contact's point and normal are the ray's (one query more than a ray on flat ground); elsewhere
— an edge, a gap — the shape at rest there gives them (two queries): the normal leans where the tyre meets an edge, and
the tyre's forces are in that plane. The swept shapes are made on the main thread when the contact or the size changes.

## Checks (cloud: the CPU)

`vehicle_check.gd`, the editor build and the release template, on raised roads (slabs 0.3 m high): a curb 12 cm high
driven onto at 15 km/h, a trench 25 cm wide and 30 cm deep crossed at 10 km/h, each by a car on rays and one whose
tyres are cylinders:

| | rays | cylinders, GodotPhysics3D | cylinders, Jolt |
|---|---|---|---|
| the front suspension's largest change in a step at the curb | 0.120 m (the curb's height at once) | 0.046 m | 0.047-0.048 m |
| the body's largest vertical acceleration there | 0.70 g | 0.53 g | 0.47 g |
| the suspension's drop over the trench | 0.085 m (to full extension) | 0.015-0.018 m | 0.020-0.021 m |
| onto the curb, over the trench, on | yes | yes | yes |

The fifteen earlier lanes: the same results (GodotPhysics3D and Jolt).

GodotPhysics3D's shape casts against the bench's ground — a box 2 km across — gave erratic contacts (the suspension
flickering between 0 and 0.2 m on flat ground, with or without FellowEngine's collision changes); on slabs of 4-100 m
they are steady, and Jolt's are steady on both. The lanes are on slabs; the documentation says to sweep against ground
of a sensible size with GodotPhysics3D.

## Cost

`--many=64 --contact=2` on the 2 km ground, the release template on the cloud's two cores: a car's step 9 → 32 µs with
GodotPhysics3D, 11.5 → 87 µs with Jolt (its shape cast a bisection of about ten collision tests; four wheels, two
queries on an edge). For the player's car and those near it, rays for traffic.

## Decision

Keep, off by default: a car that meets a curb with its tyres.

## Rejected / not done

* **Jolt's own shape cast** (`CastShape`, one sweep instead of Godot-Jolt's bisection): Godot-Jolt's semantics kept.
* **Switching contacts by distance** (shapes near the camera, rays far): the game sets `contact_shape`.
* **Several contact points a tyre** (a tyre across a ridge): one.
