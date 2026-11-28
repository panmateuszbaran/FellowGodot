# FellowEngine — milestone 248 report (glTF's material extensions)

Status: **FINAL** (2026-11-28). Branch `fellow/geometry`. Decision 0390. Checked on lavapipe against Khronos' glTF
sample assets; the RTX measurements of 241-248's open items are queued (`m248_rtx_job`).

## 0. Result

Assets authored in Blender, Substance or other glTF tools keep their layers: with
`rendering/fellow/shading/gltf_material_extensions` on, glTF's sheen, clear coat, anisotropy, IOR and specular
extensions are read into BaseMaterial3D and written back from it, and files that require them load. A velvet sofa that
came in grey is blue velvet, ToyCar's dark cloth red, brushed metal's highlights stretch, a car's paint has its coat.
BaseMaterial3D gains `sheen_texture` (the sheen's colour in RGB, its roughness in A).

## 1. Changes

* `modules/gltf/fellow_gltf_materials.cpp` (new): the extensions' reading and writing, the textures packed or baked
  where Godot reads them differently (the coat's strength and roughness into one, the anisotropy's rotation and
  direction into the flowmap).
* `modules/gltf/gltf_document.{h,cpp}`: the hooks in `_parse_materials` and `_serialize_materials`, the five extensions
  listed as supported when the setting is on.
* `scene/resources/material.{h,cpp}`: `BaseMaterial3D.sheen_texture` (a key bit, a sampler only when it is set).
* `servers/rendering/fellow_settings.cpp`: `shading/gltf_material_extensions` (off).
* Docs: decision 0390, OPTIONS, the settings' and BaseMaterial3D's reference, README, the changelog,
  `docs/showcase/gltf_materials_m248.jpg`.

## 2. Checks (lavapipe)

* 16 of Khronos' sample assets rendered with the setting off and on against their reference screenshots: the sheen
  (CompareSheen, SheenTestGrid, SheenChair, SheenCloth, GlamVelvetSofa, ToyCar), the coat (ClearCoatTest,
  CompareClearcoat, ClearCoatCarPaint, ToyCar), the anisotropy (AnisotropyStrengthTest, AnisotropyRotationTest,
  AnisotropyDiscTest, CompareAnisotropy, AnisotropyBarnLamp), the specular (SpecularTest, CompareSpecular). Where Godot's
  model can, the images follow the references; where it cannot (rougher coats, a coat's own normal map, a mirror
  stretched by anisotropy, tinted or chrome-bright dielectric specular) decision 0390 says so.
* SheenTestGrid and ClearCoatCarPaint *require* their extensions: off they do not load (as stock), on they do.
* Export and load again: the materials' values the same, the renders 78-84 dB apart (one 51.7 dB: a flowmap quantized
  twice).
* The editor's import with the setting on: the extensions in the materials, the packed textures embedded.
* The Mobile renderer's image with a sheen texture identical to without; Compatibility without errors.
* Loading cost: the barn lamp +130 ms (a 2048² texture baked), others +0.2-5 ms.
* The image regression suite: 48 scenes, exact.

## 3. Queued for the RTX (`.tools\m248_rtx_job.bat`)

The materials' layers' GPU cost on a screen of them at 1440p (base, sheen, sheen with its texture, coat, anisotropy,
all: decisions 0383 and 0390); the square by day with and without its people (0389 against 0374's +1.7 ms); the sun's
depth fit (0388) off and on in the square, the osteria and the demo's climb, with frames of each.
