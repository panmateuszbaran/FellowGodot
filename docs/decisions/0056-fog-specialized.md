# 0056 — Volumetric fog: the density pass without the loops a frame does not run

Status: **KEEP, on by default from `fellow138`** (exact; measured with `fellow136`) · Branch `fellow/geometry` · Setting `rendering/fellow/postfx/fog_specialized`
(runtime) · Code: `shaders/environment/volumetric_fog_process.glsl` (`sc_fellow_no_area_lights`,
`sc_fellow_no_voxel_gi`), `environment/fog.{h,cpp}` (`fellow_spec_pipelines`), `fellow_settings.*`;
`benchmarks/project/scenes/lights_lab.gd` (`--area_lights=N`, for the tests).

## Problem

The density pass of volumetric fog (`volumetric_fog_process.glsl`, `MODE_DENSITY`) lights every froxel with the
directional, omni, spot and area lights of its cluster cell, and adds the VoxelGI instances' light. The area light path
is the largest: a linearly transformed cosine integration of the light's quad (clipped to the horizon: arrays of five
vertices, a switch over the clipping cases, an edge integral per vertex), a texture lookup, a shadow lookup. It is
compiled into the pass whether or not the frame has area lights, and a shader is compiled for its largest live set:
the registers the area path needs limit how many warps run the omni and spot loops, which wait on light data and
shadow lookups. Stock already removes area lights from the scene shader when the cluster holds none
(`cluster_has_area_light`, a specialization constant of the colour pass); the fog's pass has no such form. None of the
benchmark scenes has area lights or VoxelGI; volumetric fog is on in Lights Lab, Sponza and Bistro exterior (density
pass 0.26 ms of a 2.35 ms frame in Lights Lab, decision 0053).

## Change

Three specialization constants of the fog process shader: no area lights (the area light loop is not compiled), no
VoxelGI and no directional lights (their loops' bounds become 0). The density pass uses a pipeline with them when the
frame's light cluster holds no area light element (`get_cluster_count_by_type(ELEMENT_TYPE_AREA_LIGHT) == 0`: every
area word of every cell is then zero, and the loop has no iteration), when no VoxelGI instance is injected
(`max_voxel_gi_instances == 0`, the loop's bound) and when the frame has no directional light
(`directional_light_count == 0`, the loop's bound; Lights Lab). Every other path, and its arithmetic, is unchanged;
stock's pipeline sets none of the constants. Up to eight pipelines per density variant, created when first used.

## Verification

lavapipe, 960 × 540, Lights Lab captures (fog on), specialized against stock's pipeline: **0 differing pixels** (no area
light, VoxelGI or directional light: all three constants set); with six area lights (`--area_lights=6`, three of them
shadowed: the area constant not set): **0**. Negative control (the area loop removed while the six area lights are
present): 713 549 differing pixels, up to 20 levels: the area lights' fog is in these images, and a wrong condition
would show.

RTX (`m11_f158_fogsc_temporal`, 60-frame sequences, specialized against stock's pipeline): Bistro exterior **bit for
bit**; Lights Lab 102.4 dB, Sponza 75.5 dB, their run-to-run floors (Sponza compiles 23 pipelines during its sequence,
Lights Lab 3, Bistro exterior none: decision 0054).

## Measured

In-process A/B (`m11_f158b_fogsc_ab`, 4 × 4 blocks per configuration, profiled; the density pass is "Render Fog"):

| scene | constants that apply | density pass (ms) | GPU |
|---|---|---|---|
| Lights Lab | area, VoxelGI, directional | 0.200 → **0.159 (−21 %)** | **−0.9 %** (p 0.001) |
| Sponza | area, VoxelGI | 0.100 → **0.073 (−27 %)** | −0.1 % (n.s.) |
| Bistro exterior | area, VoxelGI | 0.140 → **0.104 (−26 %)** | +0.1 % (n.s.) |

The area light path alone (Sponza, Bistro exterior: they have a directional light) costs a quarter of the pass. The
pass runs beside async compute's part, so a shorter pass shows in the frame only where it was the longer queue (Lights
Lab). With 0053's subgroup loop on top: Lights Lab's pass 0.151 ms (−25 %) but the same frame (−0.9 %), Sponza and
Bistro exterior slower than without it. KEEP, on by default; 0053 stays off.
