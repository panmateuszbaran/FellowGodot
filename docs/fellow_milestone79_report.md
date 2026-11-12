# FellowEngine — milestone 79 report (vehicles' marks, smoke and sounds)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0187-vehicle-effects.md`.

## 0. Result

* **`FellowVehicleEffects3D`**: a car's skid marks where its tyres slide past their grip (ribbons in one mesh, written
  in place), its tyre smoke (GPU particles), and its sound synthesised from its state with no assets — the engine's
  note from the revolutions and throttle, the tyres' squeal, the road's rumble — heard through milestone 78's
  acoustics.
* **Checked**: five lanes with GodotPhysics3D and Jolt (no marks cruising or with the anti-lock brakes; marks and smoke
  in a handbrake turn, a burnout, with locked wheels), the sound measured under Movie Maker (the tone at the firing
  frequency, the squeal), filmed on lavapipe, the editor ("Add Car" includes it).

## 1. Development

* `modules/fellow_world/fellow_vehicle_effects_3d.{h,cpp}`; `fellow_vehicle_3d.{h,cpp}` (the wheel's slip speed and
  contact normal); `register_types.cpp`, `config.py`, `doc_classes/FellowVehicleEffects3D.xml`, `FellowWheel3D.xml`,
  an icon.
* `modules/fellow_editor/fellow_editor_plugin.cpp`: "Add Car" with the node.
* `benchmarks/project/bench/vehicle_fx_check.gd`; the editor check: the icon. The acoustics bench stops its sounds
  before quitting (a player still playing at quit keeps its playback: stock's leak warning).

## 2. Verification

* The lanes on the editor build (cloud) with GodotPhysics3D and Jolt, and on the release template: as expected
  (decision 0187's tables); decision 0185's vehicle lanes and regression scene unchanged.
* The sound under Movie Maker; its spectrogram looked at (harmonics through the gears, the shifts, the squeal).
* Lavapipe: the marks and smoke filmed. The editor check passes.
* The laptop: not run (not linked to the session).

## 3. Measurements

Marks and smoke 0.5-1.1 µs a car a frame; the sound 46 ns a sample, 37 µs a frame for each car within
`sound_distance` (editor build, the cloud's CPU).

## 4. Next

Dust and gravel by the ground's material, marks that fade, sampled engines as an option.
