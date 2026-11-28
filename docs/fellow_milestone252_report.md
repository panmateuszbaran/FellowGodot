# FellowEngine — milestone 252 report (glTF's diffuse transmission)

Status: **FINAL** (2026-11-28). Branch `fellow/geometry`. Decision 0393's addendum (and 0386's second addendum).

* `KHR_materials_diffuse_transmission` read as BaseMaterial3D's backlight (`fellow_gltf_materials.cpp`): the factor
  times the colour, their textures baked into the backlight texture; the albedo's multiplication undone for a plain
  albedo colour. With it, glTF's PBR extensions BaseMaterial3D can show are all read but dispersion.
* Checked on lavapipe with the sun behind Khronos' samples: the teacup's porcelain glows through, the test grid's
  factor and texture rows follow the reference (its red-on-teal row cannot: decision 0393).
* Tried and rejected: a Kajiya-Kay hair shader for the people (decision 0386): white streaks or a grey wash where the
  anisotropic GGX band reads as hair.
