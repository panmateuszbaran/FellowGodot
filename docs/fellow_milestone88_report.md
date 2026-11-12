# FellowEngine — milestone 88 report (vehicles on loose ground, marks that fade)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0196-vehicle-dust-and-fading-marks.md`.

## 0. Result

* **Loose ground** (colliders in the group `fellow_loose_ground`): a car's rolling wheels raise dust — more with the
  speed and the sliding, its colour from the ground's metadata — and leave ruts; no tyre smoke there.
* **Marks that fade** (`mark_lifetime`): skid marks fade out instead of vanishing when their segments are reused.
* **Checked**: two new lanes on both physics engines and the release template, decision 0187's five unchanged,
  filmed; the regression scene `vehicle` exact; decision 0185's vehicle check passes.

## 1. Development

`modules/fellow_world/fellow_vehicle_effects_3d.{h,cpp}`, `fellow_vehicle_3d.h`,
`doc_classes/FellowVehicleEffects3D.xml`; `benchmarks/project/bench/vehicle_fx_check.gd` (two lanes, the pictures'
camera wider).

## 2. Verification

Decision 0196's table; the laptop: not run (not linked to the session).

## 3. Measurements

The dust: a GPU particle system a wheel, made with the node; the fading: one attribute update of the marks' mesh at
most four times a second.

## 4. Next

Gravel thrown, deformed ground, surfaces by physics material.
