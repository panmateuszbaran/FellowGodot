# FellowEngine — milestone 110 report (rigid bodies by the thousand: a measurement)

Status: **FINAL** (2026-11-13): the owner said "ok kontynuuj"; the laptop's jobs f416-f425 done. Branch
`fellow/geometry`. A measurement (the readiness assessment listed "thousands of bodies not measured"), and four
additions to the vehicles' damage: a regression scene, the editor's car preset, engine smoke, suspension damage,
hinged parts.

## 0. Result

Physics Lab's `pile` (decision 0085's case: boxes, spheres, capsules, cylinders and convex hulls dropped from a grid
into a 20 × 20 m bin, piling up and settling; `--physics_scale` 1, 2.5 and 5; no meshes) on the laptop (24 threads),
600 ticks at 60 Hz, `benchmarks/physics_bench.py`, stock Godot 4.7.2's release template against FellowEngine's
(milestone 109's), ms a tick (the mean over the 600 ticks, the whole frame):

| bodies | GodotPhysics3D stock | FellowEngine | | Jolt stock | FellowEngine |
|---:|---:|---:|---:|---:|---:|
| 2 000 | 26.5 | 12.2 | × 2.18 | 3.09 | 3.11 |
| 5 000 | 106.8 | 40.0 | × 2.67 | 10.26 | 10.46 (three runs each: the same within the laptop's spread) |
| 10 000 | 395.3 | 143.6 | × 2.75 | 24.39 | 24.28 (Jolt's limits raised, below; three runs each) |

* **The same results**: FellowEngine's hashes of every body's transform (every 30 ticks) equal stock's in every case.
* **GodotPhysics3D** grows faster than the bodies (5× the bodies, 12-15× the time): at 10 000 its step's phases are
  the broadphase 63.0 ms, the solve 29.2, the pairs' setup 23.5, the islands 14.9, integration 7.6. FellowEngine's
  milestone-17 work (decisions 0085-0091) gains more the larger the pile (× 2.2 → × 2.75), but 10 000 bodies in one
  pile are beyond a frame.
* **Jolt** grows with the bodies (5× the bodies, 8× the time): 10 000 in one pile in 24 ms a tick, of which 13-17 ms
  Jolt's step and 8 ms the bodies' transforms copied to their 10 000 nodes.
* **Jolt's limits** (a finding for games, stock's as well as FellowEngine's): at Godot's defaults (65 536 body pairs,
  20 480 contact constraints) the 10 000-body pile overflows Jolt's buffers ("contacts were ignored": bodies sink into
  each other) and the simulation is no longer repeatable — stock's own runs differ from tick 180 on (job f417). With
  262 144 body pairs and 81 920 contact constraints the runs repeat exactly (job f419); Jolt's temporary memory (32 MiB)
  then overflows into a slower allocator, 128 MiB removes it (job f420: 25.7 → 24.4 ms). A game with thousands of
  bodies in contact raises `physics/jolt_physics_3d/limits/*`.
* Job f416's single runs showed FellowEngine's Jolt 3.5-8.6 % slower at 5 000 and 10 000 bodies; three alternating
  runs (job f417) put it within the laptop's spread (stock 20.5-22.3 ms, FellowEngine 21.8-22.8 ms at 10 000 with the
  default limits), and neither Jolt's job count at the worker count nor decision 0088's sensor contacts changed it.

## 1. The regression suite

A scene for milestones 105-109: `vehicle_damage` (group `base`) — a parked car hit four times as it starts
(`apply_impact()`): its front dented, its front-left wheel's toe bent, its bumper hanging from its left end, its right
mirror off and falling. Exact on lavapipe run to run, the release template the same as the editor build; the other 35
scenes of the suite exact after milestones 107-109.

## 2. The editor

The FellowEngine menu's "Add Car" makes a car that takes damage (milestones 105-109): its body a box of 8 × 4 × 20
quads (vertices to dent), a bumper, a `FellowVehicleDamage3D` (`wheel_bend` 4°, `engine_power_loss` 0.4, the bumper
hanging at 6 cm and off at 16 cm). The editor check: the preset's 15 nodes added without a configuration warning, owned
by the scene, undone; 37 icons.

## 3. Engine smoke

`FellowVehicleDamage3D.engine_smoke` (on): with `engine_power_loss` set, a hurt engine smokes — a `GPUParticles3D`
made when the node enters the tree (its materials' shaders shared by key), dark smoke rising from above
`engine_position`, its amount the dent's share of `max_depth` there, from a tenth of it. `vehicle_check.gd`: the
engine lane's car smokes, the unhit, the side-hit (the engine 4 % weaker: under a tenth) and the repaired ones do not;
the other values as before; the editor build and the release template the same. The laptop (jobs f421-f422): the
same with GodotPhysics3D and Jolt; the picture strip's new row showed the puffs as grey squares where the smoke trailed
past the camera (a quad without a texture) — now soft round puffs (a radial gradient).

## 4. Suspension damage

`FellowVehicleDamage3D.suspension_damage` (m, 0: off): a dent reaching a wheel shortens its `suspension_length` (a
bent strut) by `suspension_damage` × the dent's share of `max_depth` × its reach, added up to `suspension_damage`;
`get_wheel_sag()`; `repair()` gives it back. `vehicle_check.gd`'s lane `sagged` (a car at rest, 0.06 m, decision
0215's side hit by the front-left wheel): the wheel's travel 3.02 cm shorter, the others' not; that corner 2.3 cm
lower, the front-right and rear-left 0.8 cm, the rear-right 0.8 cm higher (the body leaning onto it); after `repair()`
every corner within 0.1 mm of where it was. The other lanes as before; the editor build and the release template the
same; the laptop (job f423) the same values with GodotPhysics3D and Jolt.

## 5. What a car that can be damaged costs

`vehicle_check.gd --many=64 --damage=1`: 64 cars driving circles, each with a `FellowVehicleDamage3D` with every
feature on (contact reporting, the steps' impulses read; no hit), against the same without it; the laptop (job f424,
three alternating runs each, the release template, ms a frame): GodotPhysics3D 0.825-0.847 → 0.851-0.869 (+0.025 ms,
0.4 µs a car), Jolt 0.755-0.768 → 0.766-0.783 (+0.01 ms); the vehicles' own step the same within the spread.

## 6. Hinged parts

Decision 0217's hanging parts swing every way about a pin; a part with the metadata `fellow_hinge_axis` (a direction in
its space) hangs on a `HingeJoint3D` along it instead, through the same point (the end of its longest side farthest
from its last hit). The metadata `fellow_hinge_limits` (a `Vector2` of degrees) limits the turn (the
joint's angle: positive turns the part by the left-hand rule about the axis — a left door with its axis up opens
outwards with (0, 70); the first try, (−70, 0), kept the door shut). `vehicle_check.gd`'s lane `door`: a car's left
door (0.06 × 0.6 × 1.0 m, the axis vertical) hit at its rear end at rest hangs from its front edge; the car then turns
right at 25 km/h and the door swings open — without a limit 94.8° at 4 s (GodotPhysics3D; Jolt 94.6°, the laptop's job
f425 the same), its front edge 4.7 cm from where it was (the joint's give); with (0, 70) 74.1° (the limit's give), the
edge 3.9 cm off, its rear end level. The other lanes as before; the editor build and the release template the same.

## 7. Next

GodotPhysics3D's broadphase at 10 000 bodies (63 ms); the 8 ms of node updates at 10 000 bodies (both engines).
