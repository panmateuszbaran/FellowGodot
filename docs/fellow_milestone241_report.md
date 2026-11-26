# FellowEngine — milestone 241 report (sheen for cloth)

Status: **FINAL** (2026-11-26). Branch `fellow/geometry`. Decision 0383. No GPU work: checked on lavapipe.

## 0. Result

The first of the medium gaps after milestone 239 (the owner: "close the medium ones for now"): materials. Godot's
material model had no lobe for fabric; every AAA engine has one (Unreal's Substrate fuzz, Filament's sheen, glTF's
`KHR_materials_sheen`). Velvet, wool, felt and satin can now have a sheen: their fibres catch light at grazing angles,
their rims and folds brighten, and the base under them keeps exactly the energy the sheen leaves (a white furnace
gives the same pixels with and without). The showcase's suits and felt hats wear it.

## 1. Changes

* `servers/rendering/shader_types.cpp`: the fragment built-ins `SHEEN` (vec3) and `SHEEN_ROUGHNESS` (float).
* Forward+ (`scene_shader_forward_clustered.cpp`, `scene_forward_clustered.glsl`, `scene_forward_lights_inc.glsl`):
  written, they define `LIGHT_SHEEN_USED`; each light adds the sheen's lobe and scales its base contribution, the
  ambient light the same (the sheen lit by the occluded irradiance), the sheen's albedo from Godot's DFG table (its
  cloth channel, computed and unused until now).
* Mobile and Compatibility: the built-ins accepted and ignored.
* `BaseMaterial3D`: the Sheen group (`sheen`, `sheen_color`, `sheen_roughness`), documented in its class reference.
* Showcase: `FellowPerson.cloth_sheen` (suits and felt hats: 0.3 × the clothes' tint, roughness 0.5);
  `person_look.gd --sheen=0`.
* Docs: decision 0383, `FELLOWENGINE_OPTIONS.md` (Cloth sheen), the changelog, `docs/showcase/sheen_m241.jpg`.

## 2. Checks

* The test scene's spheres and cloth (the image above): rims and grazing folds brighter, head-on faces unchanged.
* White furnace: identical pixels with and without a white sheen at four roughnesses.
* Depth, shadow, unshaded variants compile; Mobile and Compatibility draw the material without the sheen.
* The cast: 44.4 dB against no sheen, a suit from the side 46.7 dB (subtle, as cloth is).
* Regression suite: 48 of 48 scenes exact.

## 3. Open

* Sheen maps (glTF's `sheenColorTexture`, `sheenRoughnessTexture`) and the import of `KHR_materials_sheen`.
* The sheen in the Mobile renderer.
* On the RTX: its cost (a few ALU per light on the materials that use it; none elsewhere).
