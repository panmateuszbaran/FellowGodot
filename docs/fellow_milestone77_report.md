# FellowEngine — milestone 77 report (vehicles)

Status: **FINAL** (2026-11-12; the laptop's results 2026-11-13, job f396): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0185-vehicles.md`.

## 0. Result

* **`FellowVehicle3D` and `FellowWheel3D`**: cars and trucks on GodotPhysics3D and Jolt — wheels on rays with springs,
  dampers, anti-roll bars and bump stops; tyres with a combined-slip curve on the ground's friction; an engine with its
  torque curve, limiter, braking and inertia; an automatic gearbox (earlier shifts at part throttle, kickdown, reverse
  from the brake), open or locked differentials, traction control and anti-lock brakes; the wheels' spin integrated
  implicitly, a hold at a standstill (slopes, moving platforms), moving and rigid ground.
* **Checked**: fifteen lanes with both engines (acceleration, braking, a skidpad, a lane change, bumps, a handbrake
  turn, reversing, a jump, a turntable, a slab pushed back, a parked car and a free one on a slope, ice, coasting);
  many cars at once; a regression scene; the editor ("Add Car", the wheel's gizmo, the icons).

Also in this milestone: milestone 76's laptop results (job f395; decisions 0184 and 0182).

## 1. Development

* `modules/fellow_world/fellow_vehicle_3d.{h,cpp}`: the two nodes; `register_types.cpp`, `config.py`,
  `doc_classes/FellowVehicle3D.xml`, `FellowWheel3D.xml`, icons.
* `modules/fellow_editor/fellow_editor_plugin.{h,cpp}`: the wheel's gizmo, "Add Car".
* `benchmarks/project/bench/vehicle_check.gd` (the lanes, `--many=N`, pictures); `tools/fellow_regression`: the scene
  `vehicle`; the editor check: the icons, the gizmo.

## 2. Verification

* The fifteen lanes on the editor build (cloud) with GodotPhysics3D and Jolt: all as expected (decision 0185's table);
  the release template's results the same.
* Many cars at once (64 and 256 driving circles): all upright, both engines.
* Lavapipe: the lanes filmed; the regression scene `vehicle` exact on a second run and from the release template; the
  whole suite (32 scenes) with the release template.
* The editor check: the icons, the wheel's gizmo, "Add Car" without a warning and undone.
* The laptop (job f396, milestone 97; a release template of 9a49b088a1, MSVC, the RTX 5070 Ti Laptop): the fifteen lanes
  as expected with both engines, the same as the cloud's within floats' rounding (the acceleration lane's top speed
  205.521 against 205.523 km/h); 64 cars 0.52 ms of vehicles a frame with GodotPhysics3D and Jolt (the frame 0.86 and
  0.76 ms), 256 cars 1.94 and 2.09 ms (2.77 and 2.78 ms; the 99th percentile 4.4 and 5.1 ms), 7.6-8.2 µs a car's step,
  all upright; the lanes filmed on the RTX (`vehicle_strip.png`).

## 3. Measurements

A vehicle's physics step (four rays, four substeps): 8.1 µs on the cloud (editor build); 64 cars 0.65 ms a frame with
GodotPhysics3D and 0.80 ms with Jolt, 256 cars 2.9 and 3.2 ms (release template, the main thread).

## 4. Next

Shapes cast for the wheels, surface effects (skid marks, dust, sounds from the slips and loads), two-wheelers.
