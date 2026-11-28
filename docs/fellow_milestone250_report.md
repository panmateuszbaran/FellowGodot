# FellowEngine — milestone 250 report (iridescence)

Status: **FINAL** (2026-11-28). Branch `fellow/geometry`. Decision 0392 (and decision 0386's addendum). Checked on
lavapipe; its cost joins the RTX measurements.

## 0. Result

BaseMaterial3D gets a thin film: soap bubbles, oil on water, tempered and anodized metal, shells and pearl take
their colours, changing with the angle and the film's thickness (Belcour and Barla's model, glTF's
`KHR_materials_iridescence`, read and written with decision 0390's setting). One of the layers AAA materials have —
Substrate's and HDRP's thin film — that Godot's did not.

Also tried and rejected: the people's hair as cards made from its traced guides (decision 0386's addendum) — not
better than MakeHuman's painted shell at 0.6 m.

## 1. Changes

* Forward+: `IRIDESCENCE`, `IRIDESCENCE_IOR`, `IRIDESCENCE_THICKNESS` (nm); `fellow_thin_film_fresnel` evaluated once
  per pixel at the view's angle, mixed into each light's and the ambient light's specular Fresnel. Mobile and
  Compatibility accept the built-ins and ignore them.
* BaseMaterial3D: the Iridescence group (`iridescence`, `iridescence_strength`, `iridescence_ior`,
  `iridescence_thickness_min`, `iridescence_thickness_max`, `iridescence_texture`).
* glTF: `KHR_materials_iridescence` read and written (`fellow_gltf_materials.cpp`).
* Docs: decision 0392, 0386's addendum, OPTIONS, the class reference, the settings' description, README, the changelog,
  `docs/showcase/iridescence_m250.jpg`.

## 2. Checks (lavapipe)

* A grid of filmed spheres (thickness × IOR, dielectric and metal): the interference orders' colours; a film matching
  its substrate's index invisible.
* Khronos' samples against their references: the metallic spheres' bands, Suzanne's oily metal (requires the extension:
  loads only with the setting on), CompareIridescence's rim, the abalone's faint film under a dim sky.
* Export and load again: the film's values kept.
* The image regression suite: exact.
