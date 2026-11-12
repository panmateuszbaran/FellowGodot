# FellowEngine — milestone 90 report (occlusion by material)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0198-acoustic-materials.md`.

## 0. Result

* **Occlusion by material**: a collider's metadata `fellow_occlusion` (0 to 1) says how much it blocks sound — a
  source behind glass 0.3 is muffled −3.6 dB (−7.6 dB on noise in the mix), behind a wall −12 dB (−27 dB) as before.
* **Checked**: a glass lane on GodotPhysics3D and Jolt and in the mix under Movie Maker; decision 0186's lanes
  unchanged.

## 1. Development

`modules/fellow_world/fellow_acoustics_3d.cpp`, `doc_classes/FellowAcoustics3D.xml`;
`benchmarks/project/bench/acoustics_check.gd` (a glass lane, listened too).

## 2. Verification

Decision 0198's table; the laptop (job f396, milestone 97): as expected with both engines, exactly the cloud's results
(under Movie Maker glass −28.2 dB, a curtain −31.8 dB).

## 3. Measurements

One metadata lookup a blocked ray.

## 4. Next

Several walls on a path, transmission by frequency, diffraction paths and portals.
