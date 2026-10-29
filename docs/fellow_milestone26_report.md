# FellowEngine — milestone 26 report (wind in the forest)

Status: **measured on lavapipe** (2026-10-29; the user asked not to load the laptop's GPU for now: no RTX job yet).
Branch `fellow/geometry`. Decision record `docs/decisions/0125-wind.md`; user-facing summary
`docs/FELLOWENGINE_OPTIONS.md` ("Wind").

## 0. Result

The user asked (2026-10-29), after rain (milestone 25), for wind. Stock Godot has none: projects sway vegetation in
vertex shaders of their own, a sine of `TIME` per plant, the whole plant as one.

`FellowWind3D` (module `fellow_fluid`) is the scene's wind, with **gusts** — a noise field travelling with it — that
roll over a meadow in waves. Vegetation moves with it in the vertex shader of every pass (Forward+):

* a `BaseMaterial3D` gets a **Wind** group (height of the plant, bend, flutter, sheen), a `ShaderMaterial` the render
  mode `fellow_wind` and four vertex built-ins;
* each instance is a plant: it **bends** with the wind's force towards its top, **sways** at a frequency of its size,
  its **branches** at their own phases, its **leaves flutter**; the normals turn with the bend and bent grass
  brightens (a **sheen**: the gusts' waves become visible);
* the shadows and the **motion vectors** follow (the previous frame's wind is kept): TAA and motion blur see it;
* the rain's drops follow the same wind.

## 1. Development

* **Wind Lab** (`--scene=wind`): a meadow (about 40 k grass clumps of 16 blades) in a ring of 700 broadleaf trees
  (trunk, seven branches, leaf cards with an alpha-scissor texture of leaves) and 500 bushes; `--wind=on|off|shader`
  (shader: the TIME-based sway projects write, for comparison), `--wind_speed`, `--wind_gustiness`, `--wind_gust_size`,
  `--wind_grass_bend`, `--wind_grass_sheen`; still-camera sequences of the meadow, over the meadow and of a tree.
* The gusts first did not show: the field was there (a debug view of it thresholded showed its patches moving with
  the wind) but bent grass looked like upright grass. Three causes: the bend's soft limit compressed the gusts' force
  (grass bend 1.4), the sparse thin blades let the ground dominate the view from above, and the blades' brightness
  hardly depends on their lean. Fixed with a less saturated bend (1.0), more contrast in the gusts, denser grass in
  the lab, the normals turned with the stem, and the sheen.
* The displacement is added in view space after the model-view transform: no inverse of the model matrix per vertex.

## 2. Verification

* Without a FellowWind3D and without wind materials: City Lab, Water Lab, Rain Lab `--rain=off` and Foliage Lab with
  its TIME-based sway (`--foliage_wind=1`) bit for bit the images of `d8f2909` (the user varyings moved one location
  and the weather's uniform buffer grew: no pixel changed).
* The gusts on the CPU (`get_velocity_at`) and on the GPU from the same texture and mapping.

## 3. Performance

lavapipe (480 x 270, Wind Lab with one grass clump per m², GPU time of the viewport in ms; details in 0125):

| Wind Lab | flythrough | meadow | edge | forest | aerial |
|---|---:|---:|---:|---:|---:|
| still | 840 | 587 | 216 | 180 | 862 |
| the TIME-based sway projects write | 2079 | 1974 | 1484 | 1000 | 1505 |
| FellowWind3D, first build | 2304 | 2151 | 1694 | 1165 | 1710 |
| FellowWind3D (culling bound, far cascades cached) | **1303** | **1067** | **623** | **555** | **1016** |

The cost is in the shadows (moving casters leave the shadow cache): two optimizations made during the milestone —
MultiMesh culling with the wind's bound on the displacement instead of drawing wind MultiMeshes whole, and the far
cascades (from the third) cached with the plants' pose of their last build — took 41-63 % off. FellowWind3D now costs
32-58 % less than the sway projects write for the same scene. The RTX measurement (laptop) is pending.

## 4. Left for later

* The Mobile and Compatibility renderers; SDFGI's voxelization of moving plants; particles in the wind; the water's
  waves from the same wind; the RTX measurement.
