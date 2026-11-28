# 0392 — Iridescence: a thin film over the surface

Status: **KEEP** (a material property: off unless a material turns it on) · Branch `fellow/geometry` · Code:
`servers/rendering/shader_types.cpp` (`IRIDESCENCE`, `IRIDESCENCE_IOR`, `IRIDESCENCE_THICKNESS`),
`renderer_rd/shaders/scene_forward_lights_inc.glsl` (`fellow_thin_film_fresnel`, the lights' Fresnel),
`renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl` (the film's Fresnel, the ambient light's),
`scene_shader_forward_clustered.cpp`, the Mobile and Compatibility renderers' renames, `scene/resources/material.{h,cpp}`
(BaseMaterial3D's Iridescence group), `modules/gltf/fellow_gltf_materials.cpp` (`KHR_materials_iridescence`)

## Problem

Decision 0390 left glTF's iridescence unread: BaseMaterial3D had no thin film. It is one of the layers AAA materials
have and Godot's do not (Unreal's Substrate and Unity HDRP's iridescence): soap bubbles, oil on a puddle, tempered and
anodized metal, beetles' shells, mother-of-pearl, car paints that turn from violet to green.

## Change

* **Shading** (Forward+): a film of index `IRIDESCENCE_IOR` and thickness `IRIDESCENCE_THICKNESS` (nm) over the
  surface. Its reflectance is Belcour and Barla's (2017): the Airy sum's DC term and its first two orders, each order's
  spectral integral by a Gaussian fit of the CIE matching functions, converted to linear Rec. 709; the surface under
  the film keeps its F0 (its IOR from it; a metal's per channel); as the film thins to nothing its index goes to the
  outside's (no colours). As glTF's reference viewer does, the film's Fresnel is evaluated once per pixel at the view's
  angle and mixed by `IRIDESCENCE` into the specular Fresnel of every light and of the ambient light (whose split-sum
  takes it as the angle's Fresnel times the DFG table's scale).
* **BaseMaterial3D**: `iridescence` (the group's switch), `iridescence_strength`, `iridescence_ior` (1.3),
  `iridescence_thickness_min`/`_max` (100/400 nm) and `iridescence_texture` (R the strength, G the thickness from min to
  max — glTF's two textures packed into one).
* **glTF** (with `shading/gltf_material_extensions`, decision 0390): `KHR_materials_iridescence` read and written; the
  two textures taken as they are when they are one, packed otherwise.
* Mobile and Compatibility take the built-ins and ignore them (like the sheen, decision 0383).

## Checks (lavapipe)

* A grid of spheres (film thickness 100-900 nm across; IOR 1.2, 1.5, 2.0 down; a black dielectric and a grey metal):
  the colours run through the interference orders with thickness, strongest over the metal; over the dielectric of
  IOR 1.5 a film of IOR 1.5 shows nothing (no second interface), as it should; without the film the same spheres plain.
* Khronos' samples against their references (setting on): IridescenceMetallicSpheres (a cube of spheres: thickness ×
  IOR × colour) shows the reference's pink, green and blue bands; IridescenceSuzanne's metal head its oily colours
  (the file requires the extension: it loads only with the setting on); CompareIridescence a violet and orange rim the
  plain sphere lacks (weaker than the reference's, whose studio environment has bright features for the film to
  colour); IridescenceAbalone a little purple where the reference is green over much of the shell (under this test's
  dim uniform sky the film's colour shows only in reflections). IridescenceDielectricSpheres' film is faint over their
  white diffuse base here and in the reference. The dish and the lamp have their films on glass (transmission, not
  read: decision 0390).
* The image regression suite: exact (no material in it has a film).
* Cost: one evaluation per pixel of a filmed material (two orders of the spectral sum: a few dozen ALU and two
  `exp`/`cos` triples); nothing for other materials. On the RTX +0.29 ms over a 1440p screen (below).

## RTX laptop (2026-11-28; RTX 5070 Ti Laptop, milestone 251's build)

`matcost` (a test of its own: a screen of spheres and a wall behind them at 2560×1440, every pixel the material under
test, a sun with shadows and 16 omni lights; the variants interleaved, 4 rounds of 300 frames): the base 1.79 ms,
**iridescence +0.29 ms** (2.08: the film's spectral sum once per pixel), like stock's anisotropy (+0.29 ms) and
cheaper than its clear coat (+0.45 ms); the sheen +0.13 ms; every layer at once +1.07 ms.

## Decision

KEEP. Open: transmission (decision 0390's open item: the dish's and the lamp's glass), the film's colour on the diffuse
light (glTF weighs the diffuse by what the film reflects; Godot's diffuse is not weighed by any Fresnel).

## Rejected

* **Evaluating the film per light at each light's half-vector angle**: exact for each light but the spectral sum per
  light per pixel; glTF's reference viewer uses the view's angle for all, and so does this.
