# 0393 — glTF's glass: transmission and volume

Status: **KEEP as an option** (with decision 0390's `rendering/fellow/shading/gltf_material_extensions`, off) ·
Branch `fellow/geometry` · Code: `modules/gltf/fellow_gltf_materials.cpp` (`KHR_materials_transmission`,
`KHR_materials_volume`, `fellow_material_scale`), `modules/gltf/gltf_document.{h,cpp}` (the material's index passed, the
two extensions listed), `scene/resources/material.{h,cpp}` (`BaseMaterial3D.refraction_tint`)

## Problem

Decisions 0390 and 0392 left glTF's glass unread: `KHR_materials_transmission` (light through a thin surface: windows,
bottles, glass domes) and `KHR_materials_volume` (through a solid: its thickness, the IOR's bend, the colour it absorbs
along the way — amber, coloured glass, liquids). Godot imported such materials opaque: ToyCar's windows, the
candle-holder's glass and the glass dragon came in as white or coloured plastic.

## Change

* **Transmission as BaseMaterial3D's refraction**: the screen behind the surface seen through it, blurred by its
  roughness (Godot's screen-space refraction), letting through 1 − the albedo's alpha: the transmission factor goes to
  that alpha (the transmission texture, with its factor, into the albedo texture's alpha).
* **`refraction_tint`** (new, off): the light through the surface multiplied by its albedo — coloured glass — and by
  what its surface does not reflect (1 − Schlick's Fresnel at the view's angle, F0 from its specular: glTF's 1 − F), so
  that glass shows its reflection, not what is behind it, at grazing angles (its silhouettes, a bowl's sides). Off, the
  refraction is stock's (the same shader text).
* **Volume**: thin-walled (no thickness) straight through; a volume bent by its IOR (Godot's offset, 0.05 at IOR 1.5,
  scaled with IOR − 1) and absorbing along its thickness (Beer-Lambert: the attenuation colour to the power of the
  thickness over the attenuation distance) — the thickness in the world's units (glTF's is in the mesh's: scaled by the
  first node using the material); with a thickness texture the absorption is baked into the albedo texture, texel by
  texel (thin parts light, thick ones deep).
* **Export**: a tinted refraction as `KHR_materials_transmission` (thin-walled).

## Checks (lavapipe; Khronos' samples, setting off and on, against their references)

* ToyCar: its windows open onto the seats behind them. GlassHurricaneCandleHolder: clear glass, deep blue at its thick
  base, the candle inside — close to the reference, where off it was a white vase. GlassVaseFlowers: the transmission
  vase clear with the stems inside (beside its alpha-blended twin). CompareTransmission and CompareVolume: the bowls
  clear, the volume one green with its thickness.
* DragonAttenuation: amber, darker where thick, as the reference — after the node's scale (0.25) was applied to the
  thickness (without it the path was four times too long: a dark red dragon).
* AttenuationTest: its grid of blue cubes (sample colour, thickness factor, thickness texture, node scale, attenuation
  distance) follows the reference's progressions.
* TransmissionRoughnessTest: the blur grows with roughness along the rows as in the reference; TransmissionTest's
  spheres transparent and coloured (its "with opacity" columns lose their cut-outs: see below).
* The light through weighed by 1 − F (a follow-up commit): the bowls' sides and the candle-holder's silhouette show the
  sky's reflection where they showed the background straight through.
* The image regression suite: exact.

`docs/showcase/gltf_glass_m251.jpg`: ToyCar, GlassVaseFlowers and TransmissionTest (Khronos' samples, CC0) with the
setting off and on.

## Limits

* Screen-space: only what was drawn before the glass shows through it (no glass behind glass, nothing off screen).
* Refractive materials cast no shadow here (real glass casts a light, tinted one).
* A transmissive material's alpha cut-out (`alphaMode` MASK with transmission) is lost: the albedo's alpha now carries
  the transmission.
* No dispersion, no thickness for the bend (only the IOR).

## Decision

KEEP, with decision 0390's setting (off). Open: shadows through glass (a tinted, lighter shadow), glass seen through
glass (ordering, or a refraction pass per layer).
