# FellowEngine — milestone 26 report (wind in the forest)

Status: **measured** — lavapipe (2026-10-29) and the RTX 5070 Ti Laptop at 1080p (`m26_f275`, 2026-10-30).
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
32-58 % less than the sway projects write for the same scene.

RTX 5070 Ti Laptop (1080p, the full meadow, 3 runs, `m26_f275_wind`): still 3.566 ms of GPU time, FellowWind3D
4.122 ms (+0.56: the near shadow cascades +0.33, the opaque pass +0.23 — two poses per vertex for the motion vectors —,
the pre-pass +0.06), the TIME-based sway 5.372 ms (+1.81). The wind with gusts is 1.25 ms (23 %) cheaper than what
projects write.

## 4. Optimizations of the new features (2026-10-29, afternoon and evening)

The user asked to keep optimizing what milestones 24-26 added (lavapipe only: the laptop's GPU not to be loaded), in
order: the wind's shadows, the wind's vertex cost, the rain, the water's reflections.

| | change | effect (lavapipe unless noted) | decision |
|---|---|---|---|
| 1 | wind: MultiMesh culling with the wind's displacement bound; far cascades (from the third) cached with the plants' last pose | Wind Lab −41 to −63 % GPU time against the first build; −32 to −58 % against the TIME-based sway projects write | KEEP (0125) |
| 2 | wind: the vertex work without shadows | 0-25 % of the still scene; nothing left worth a change once the shadows are cached | — |
| 3a | rain: the ripples' compute — the 75 cells' drops worked out once per group in shared memory instead of 27 times per texel; wraps without integer modulo | exact (Rain Lab without drops bit for bit); the ripples' compute 14.1 → 9.4 ms on lavapipe (−33 %; on the RTX it is 0.04 ms) | KEEP (0124) |
| 3b | rain: the wetting, SSR on the wet surfaces (lavapipe pass timestamps: depth pre-pass +4 ms, SSR +6.5 ms of the wetting's) | nothing more that keeps the image: SSR's share comes with the wet roughness below its cutoff (walls included); the wetting's lookups (0124: explicit levels, the noise only where a puddle can be) are at their minimum | — |
| 4a | water: a Fresnel-scaled step count for the reflection march | no measurable change (+2-9 %, the spread) | reverted |
| 4b | water: reflections traced over a Hi-Z pyramid of the depth texture (`postfx/depth_copy_nearest_mipmaps`) | RTX 1080p: the reflections 0.99 → 0.61 ms (−38 %), Water Lab's GPU time 3.128 → 2.762 ms (−12 %; shore −29 %, pier −24 %, glint −36 % of the water's pass, aerial +26 %); the far shore reflected at its true parallax, no jitter | KEEP, on (0126) |

The night of 2026-10-29/30 the user allowed the laptop's GPU (jobs `m26_f275`-`f281`):

| | change | effect (RTX 5070 Ti Laptop, 1080p) | decision |
|---|---|---|---|
| 5 | rain: the ripples' cells shared (3a) | the ripples' compute −0.025 ms; Rain Lab's frame unchanged (the basin's depth pyramid +0.047 ms) | KEEP |
| 6 | water: the rays towards the camera bounded by `reflection_distance` | aerial −0.01 ms, the rest within the spread (`m26_f276`) | not kept |
| 7 | water: the depth pyramid asked for only while reflections are traced (`560d36c`) | without reflections the depth copy 0.079 → 0.026 ms (`m26_f278`); exact | KEEP |
| 8 | wind: the shadows cached from the second cascade | −0.11 ms more than from the third, but only the nearest cascade's shadows sway | the third stays |
| 9 | depth texture: the single-level copy skipped where the pyramid replaces it (`e99e5db`) | the depth copy −0.023 ms in Water Lab and Rain Lab (`m26_f280`); exact (lavapipe: Water Lab bit for bit) | KEEP |
| 10 | water: where the shading without reflections goes (`m26_f281`, measurement builds) | of the 0.46 ms: the cascades' lookups 0.21 ms (their anisotropic filtering 0.10), the foam's noise 0.013, the caustics 0.02, the refraction 0.01; the rest the lighting | — |
| 11 | the labs the final run does not cover, before and after milestone 26 (`m26_f283`: fellow207 / fellow211, 2 runs) | frame time Fire Lab 2.914 / 2.950 ms, Burn Lab 3.669 / 3.628, Fluid Lab 2.315 / 2.311, Particles Lab 2.946 / 2.921, Blend Lab 2.008 / 2.033: within the runs' spread (none of milestone 26 runs there) | — |
| 12 | burning (milestone 22): where its drawing goes (`m26_f284`, measurement builds) | +0.84 ms of GPU time against the original meshes (the opaque pass +0.43, the depth pre-pass +0.28, the shadows +0.16); the crack cells 0.19 ms of it, the vertices' march into the voxels 0.37 ms (the opaque pass 0.27, the shadows 0.12: it ran in every pass drawing the mesh) | → 13, 14 |
| 13 | burning: the crack cells skip the neighbours that cannot be nearer (exact; lavapipe 0 pixels differ) | no gain: the pre-pass 0.358 ms with and without, the opaque pass +0.02 ms (`m26_f285`) | reverted |
| 14 | burning: the vertices march into the voxels once per frame with steps, in a compute pass, the materials reading them by `VERTEX_ID` (`fluid/burn_vertices_cached`) | Burn Lab's GPU time 2.634 → 2.139 ms (−19 %; the frame −13 %): the shadows −0.14 ms (−65 %), the opaque pass −0.27, the pre-pass −0.13; the compute pass +0.03; at 48 voxels per object −24 % (`m26_f285`); the images alike (at most 151 pixels per 1080p view differ, PSNR ≥ 81 dB) | KEEP, on (0120) |

**The defaults are not slower** (`m26_f279_final`, stock against fellow210, the nine scenes, three runs each after a
pass filling the shader caches, as `m20_f260`): FellowEngine faster in every scene and every pair of runs — Sponza
−52.8 %, Bistro exterior −29.6 %, City Lab −27.4 %, Bistro interior −24.1 %, Crowd Lab −23.0 %, Lights Lab −22.9 %,
Foliage Lab −18.0 %, PBR Lab −14.7 %, Stress Lab −10.3 % (frame time; `m20_f260`: −8.2 to −53.7 %). Again at the
end of the night with fellow212 (`d183ac7`: the copy skipped, the burning's vertices marched once per frame;
`m26_f285_final`, the same procedure): faster in every scene and every pair of runs — Sponza −54.6 %, Bistro exterior
−30.8 %, City Lab −25.1 %, Bistro interior −23.5 %, Lights Lab −22.2 %, Crowd Lab −21.8 %, Foliage Lab −16.7 %, PBR Lab
−16.0 %, Stress Lab −11.5 %.

## 5. Left for later

* The Mobile and Compatibility renderers; SDFGI's voxelization of moving plants; particles in the wind; the water's
  waves from the same wind.
* Burning: of its drawing about 0.35 ms is left (Burn Lab, RTX) — the pre-pass's normals for SSAO (the crack cells),
  the subdivided mesh, and the `discard` of the burned-through triangles, which can keep the pre-pass and the shadows
  from writing depth before the fragment shader (not measured); the compute pass could drop those triangles from the
  index buffer instead.
