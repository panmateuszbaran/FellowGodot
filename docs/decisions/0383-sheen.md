# 0383 — Sheen: a cloth's fibres in the material

Status: **KEEP** · Branch `fellow/geometry` · Forward+ · Code: `servers/rendering/shader_types.cpp` (`SHEEN`,
`SHEEN_ROUGHNESS`), `scene_shader_forward_clustered.cpp`, `scene_forward_clustered.glsl`, `scene_forward_lights_inc.glsl`,
`scene/resources/material.{h,cpp}` (`BaseMaterial3D.sheen`, `sheen_color`, `sheen_roughness`); the Mobile and
Compatibility renderers take the built-ins and ignore them

## Problem

The gap analysis put materials at "medium" against Unreal's Substrate. One lobe every AAA material model has and
Godot lacks is sheen: velvet, wool, felt, satin — fabric whose fibres stand off the surface and reflect back towards
light grazing it. Godot draws cloth as a rough dielectric (its rim light is a fake: a fixed brightening, energy from
nowhere). Godot's DFG table already integrates the sheen's directional albedo (the cloth channel of
`integrate_dfg.glsl`, used by nothing).

## Change

* **The lobe** (glTF's `KHR_materials_sheen`, Filament's cloth): `SHEEN` (a colour, 0 by default) and
  `SHEEN_ROUGHNESS` (0.3) in the fragment function; written, they define `LIGHT_SHEEN_USED`. Each light adds
  sheen colour × Estevez and Kulla's "Charlie" distribution × Neubelt's visibility × N·L, and scales its base
  contribution by what the sheen leaves it: 1 − max(sheen colour) × the sheen's directional albedo (the DFG table's
  third channel at the sheen's roughness and N·V). The ambient: the base's diffuse and specular scaled the same way,
  the sheen lit by the ambient light that reaches the surface (after ambient occlusion, before the albedo tints it —
  the lobe is broad: the irradiance, not the reflection) times its directional albedo.
* **The showcase's people** (`fellow_person.gd`): their suits and felt hats get a sheen of 0.3 × the clothes' tint,
  roughness 0.5 (`FellowPerson.cloth_sheen`; `person_look.gd --sheen=0` without).
* **BaseMaterial3D**: a "Sheen" group (`sheen`, `sheen_color`, `sheen_roughness`).
* A material that does not write `SHEEN` compiles exactly as before (all of it behind `LIGHT_SHEEN_USED`).

## Checks (lavapipe)

* **A test scene** (not committed; `docs/showcase/sheen_m241.jpg`): four spheres (red velvet without and with sheen,
  blue wool, grey satin with a sharp sheen) and a folded green cloth, a sun behind and above them, a weak key light in
  front. The sheen brightens the spheres' rims and the cloth's folds seen at grazing angles; the faces seen head-on
  barely change (the sheen's albedo is small there, and the base keeps the rest).
* **A white furnace**: white spheres (albedo 1, no specular) in a uniform ambient light, white sheen at roughness 0.1,
  0.3, 0.6 and 1: the same pixels with and without the sheen (the base keeps 1 − E, the sheen adds E: no energy
  created or lost).
* **The shader variants**: depth, shadow and unshaded passes compile with a material that writes `SHEEN` (the
  globals are declared in every pass); the Mobile and Compatibility renderers compile it and draw it without the
  sheen.
* **The showcase's people**: the cast in a row 44.4 dB against no sheen (4.8 % of pixels more than 2 levels apart), a
  suit from the side 46.7 dB: the suits' edges and folds a little lighter and greyer — a fabric, not paint.
* **Regression suite**: all 48 scenes exact (no material in it writes `SHEEN`).

Not done: sheen in the Mobile and Compatibility renderers; a sheen texture (`sheen_color` and `sheen_roughness` are
uniform: glTF's sheen maps are not imported); the reflection at the sheen's own roughness (the ambient irradiance
stands in for it).

## Decision

KEEP: a lobe every AAA material model has (Unreal's Substrate "fuzz", Filament's sheen, glTF's), off unless a material
asks for it.
