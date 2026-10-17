# 0056 — Volumetric fog: the density pass without the loops a frame does not run

Status: **measuring** (`fellow136`) · Branch `fellow/geometry` · Setting `rendering/fellow/postfx/fog_specialized`
(runtime, off until measured) · Code: `shaders/environment/volumetric_fog_process.glsl` (`sc_fellow_no_area_lights`,
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

## Measured

Pending.
