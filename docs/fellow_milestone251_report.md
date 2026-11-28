# FellowEngine — milestone 251 report (glTF's glass)

Status: **FINAL** (2026-11-28). Branch `fellow/geometry`. Decision 0393. Checked on lavapipe against Khronos' samples.

## 0. Result

glTF's glass imports as glass: `KHR_materials_transmission` and `KHR_materials_volume` become BaseMaterial3D's
refraction, tinted by the material's colour (`refraction_tint`, new), a solid one bent by its IOR and absorbing along its
thickness. ToyCar's windows show its seats, the candle-holder is clear glass going deep blue at its base, the dragon is
amber — where they were white or coloured plastic. With decisions 0390 and 0392 the importer now reads every glTF PBR
extension BaseMaterial3D can show (sheen, clear coat, anisotropy, IOR, specular, iridescence, transmission, volume).

## 1. Changes

* `modules/gltf/fellow_gltf_materials.cpp`: the two extensions (the transmission factor into the albedo's alpha, its
  texture into the albedo texture's; the volume's absorption into the albedo, per texel with a thickness texture; the
  thickness in world units by the node's scale, `fellow_material_scale`); export as thin-walled transmission.
* `scene/resources/material.{h,cpp}`: `refraction_tint` (a key bit; the refraction's shader text unchanged without it).
* `modules/gltf/gltf_document.{h,cpp}`: the material's index passed to the hook; the extensions listed as supported.
* Docs: decision 0393, OPTIONS, the class reference, the settings' description, README, the changelog,
  `docs/showcase/gltf_glass_m251.jpg`.

## 2. Checks (lavapipe)

* ToyCar, GlassHurricaneCandleHolder, GlassVaseFlowers, CompareTransmission, CompareVolume, DragonAttenuation,
  AttenuationTest, TransmissionRoughnessTest, TransmissionTest against their references (decision 0393's list).
* The image regression suite: exact.

## 3. Limits

Screen-space refraction (no glass behind glass, nothing off screen), no shadows from refractive materials, a
transmissive material's alpha cut-out lost, no dispersion.
