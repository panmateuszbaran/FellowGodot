# 0390 — glTF's material extensions: sheen, clear coat, anisotropy, IOR and specular

Status: **KEEP as an option** (off: it changes how such files look) · Branch `fellow/geometry` · Setting
`rendering/fellow/shading/gltf_material_extensions` · Code: `modules/gltf/fellow_gltf_materials.cpp` (the reading and
writing), hooks in `modules/gltf/gltf_document.{h,cpp}` (`_parse_materials`, `_serialize_materials`, the supported
extensions), `scene/resources/material.{h,cpp}` (`BaseMaterial3D.sheen_texture`)

## Problem

Godot's glTF importer reads the core metallic-roughness material and three extensions (unlit, emissive strength, the
old specular-glossiness). The layers that make cloth, car paint, lacquer and brushed metal — glTF's
`KHR_materials_sheen`, `KHR_materials_clearcoat`, `KHR_materials_anisotropy`, and the dielectric's reflectance,
`KHR_materials_ior` and `KHR_materials_specular` — are dropped on import, although BaseMaterial3D can show all of them
(the sheen since decision 0383). Blender's exporter writes them from its Principled BSDF, Substance and Sketchfab too:
a velvet sofa comes in grey, a car's paint without its coat. A file that *requires* one of them does not load at all
("required extension ... is not supported").

## Change

With the setting on, `GLTFDocument` reads them into BaseMaterial3D on import (the editor's and `append_from_file` at
run time) and writes them back on export:

* **Sheen**: `sheen`, `sheen_color` (the factor, linear → sRGB), `sheen_roughness` (glTF's perceptual roughness, the
  same Charlie lobe). Its two textures go into a new `BaseMaterial3D.sheen_texture`: RGB multiply the colour (sRGB), A
  the roughness — glTF's own packing, so one texture used for both is taken as it is; two are packed into one.
* **Clear coat**: `clearcoat_enabled`, `clearcoat`, `clearcoat_roughness`. Godot's coat roughness is linear in GGX alpha
  from 0.001 to 0.1 (glTF's perceptual roughness 0.03 to 0.32): converted (rougher coats: see the addendum). Its strength
  (R) and roughness (G) textures are packed into `clearcoat_texture` (R as it is, G converted with the factor: the two
  roughnesses are not proportional). The coat's own normal texture is not read: Godot's coat follows the geometric
  normal (glTF's too when the coat has no normal texture).
* **Anisotropy**: `anisotropy_enabled`, `anisotropy`. Godot's parameter stretches alpha to alpha / sqrt(1 − 0.9 a)
  along the tangent and alpha · sqrt(1 − 0.9 a) across; glTF's strength s raises it along the direction to
  mix(alpha, 1, s²). Matched in their ratio at the material's roughness. The rotation and the direction texture (RG a
  direction, B a strength) are baked into `anisotropy_flowmap` (RG the direction, A the strength, as Godot reads it).
* **IOR and specular**: the dielectric's reflectance at normal incidence, F0 = ((ior − 1)/(ior + 1))² × specular colour
  × specular factor, as `metallic_specular` (Godot's F0 = 0.16 · specular²). The colour's tint is averaged (Godot's
  dielectric reflectance is grey) and F0 is held at 0.16 (`specular` 1); the specular textures are not read.
* **Export**: the inverse of each (sheen colour, roughness and texture; the coat with its texture re-baked; the
  anisotropy's strength and flowmap; `metallic_specular` other than 0.5 as `KHR_materials_ior`).
* The five are listed as supported, so files that require them load.

Packed textures are new `ImageTexture`s (RGBA8, mipmapped), embedded in the imported scene as the stock
specular-glossiness conversion's are; a texture taken as it is keeps the import's compression.

## Checks (lavapipe; Khronos' glTF sample assets, each loaded at run time with the setting off and on, rendered under a sky and a sun, against Khronos' reference screenshots)

* **Sheen** (CompareSheen, SheenTestGrid, SheenChair, SheenCloth, GlamVelvetSofa, ToyCar's cloth): off, the sofa is
  grey and ToyCar's cloth dark brown (their colour is in the sheen); on, blue velvet and red cloth as in the references.
  SheenTestGrid's 4×4 grid of sheen colour × roughness matches the reference's progression (it does not load with the
  setting off: it requires the extension). SheenCloth's one texture for both is taken as it is.
* **Clear coat** (ClearCoatTest, CompareClearcoat, ClearCoatCarPaint, ToyCar): the coated column gains the coat's sharp
  reflections over its rough base; the partial coating follows its texture, the roughness variations their stripes;
  over a normal-mapped base the coat stays smooth as in the reference (the "coat normal map" row cannot match).
  ClearCoatCarPaint requires the extension: it loads only with the setting on. Godot's coat also lowers a metal base's
  F0 under it (Filament's IOR correction, stock), so CompareClearcoat's coated metal is darker than the reference's.
* **Anisotropy** (AnisotropyStrengthTest, AnisotropyRotationTest, AnisotropyDiscTest, CompareAnisotropy): the highlights
  stretch into the reference's bands and arcs, growing with the strength; the rotation and texture variants of the
  rotation test show the same bands as its plain one (as designed); the brushed disc's radial sectors appear. At a
  near-mirror roughness the stretch is weaker than glTF's (Godot's ratio is at most 10 and scales alpha; glTF's
  stretches a mirror towards roughness 1).
* **Specular** (SpecularTest, CompareSpecular): the "specular factor" row goes from black (F0 0) to the default as in
  the reference; the yellow tint and "colour factor > 1" (F0 towards 1, chrome-like) cannot match; the texture rows stay
  at their factors.
* **Round trip** (loaded with the setting on, exported to .gltf, loaded again): every material's sheen, coat,
  anisotropy and specular values the same (ClearCoatTest, AnisotropyRotationTest, SheenTestGrid, SpecularTest; one coat
  re-expressed with an equivalent texture); the renders 78-84 dB apart (AnisotropyRotationTest 51.7 dB: its baked
  flowmap quantized twice).
* **The editor's import** (a project with the setting on, three .glb files): the materials carry the extensions, the
  packed textures embedded in the imported scenes, no errors.
* **The other renderers**: the Mobile renderer's image of SheenCloth with the setting on is identical to off (it ignores
  the sheen; the texture's uniform compiles); Compatibility renders it without errors.
* **Loading** (`append_from_file` and `generate_scene`, three runs each, off → on): the barn lamp 229 → 359 ms (its
  2048² anisotropy texture baked), ToyCar 92 → 97 ms, ClearCoatTest 22 → 26 ms, AnisotropyRotationTest 19.5 → 19.7 ms.
* **Off**: `GLTFDocument` as stock (the hooks return before anything); the image regression suite passes (48 scenes,
  exact).

`docs/showcase/gltf_materials_m248.jpg`: ToyCar (CC0), GlamVelvetSofa (© 2021 Wayfair LLC, Eric Chadwick, CC BY 4.0),
AnisotropyStrengthTest (CC0, Ed Mackey) and SheenTestGrid (CC0, Eric Chadwick) from Khronos' glTF-Sample-Assets,
rendered with the setting off and on.

## Decision

KEEP as an option, off (it changes how files using these extensions look, and stock Godot ignores them); on is the
right setting for a project importing assets authored in Blender, Substance or other glTF tools. Open:
`KHR_materials_transmission` and `KHR_materials_volume` (glass: ToyCar's windows, the barn lamp's bulb), the coat's
normal texture and rougher coats (both need the coat's shading changed), the specular colour and textures, and
`KHR_materials_iridescence` (a thin-film lobe BaseMaterial3D does not have).

## Rejected

* **On by default.** Correct to the files, but FellowEngine's frame is stock Godot's unless a setting says otherwise,
  and an existing project's assets would change on their next import.
* **Matching the anisotropy by raising the roughness** (Godot's alpha set to the geometric mean of glTF's two, so both
  directions match): it changes the material's roughness for everything else (its reflections' blur, a roughness
  texture's meaning) to fix the stretch of near-mirrors; the ratio alone keeps the roughness as authored.

## Addendum (2026-11-28): rougher coats

Godot's coat roughness is the GGX alpha `0.001 + 0.099 r`; the inspector's range (0-1) stops at alpha 0.1 (glTF's 0.32),
but no shader clamps it, so the importer now goes on past 1 — to 10.1, alpha 1, glTF's roughest coat — instead of
holding coats at 0.32; with a roughness texture, the material's value is the coat's roughest and the texture's G each
texel's share of it. ClearCoatTest's "roughness variations" row: the rough stripes' reflections spread as the
reference's, where they were held glossy. Export reads the same scale back.

