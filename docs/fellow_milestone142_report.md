# FellowEngine — milestone 142 report (the frame under Vulkan's validation)

Status: **FINAL** (2026-11-18). Branch `fellow/geometry`. Lavapipe in the cloud (the laptop's GPUs not used).

## 0. Result

* **Four invalid synchronizations fixed** (decision 0267; the same pictures bit for bit), found by running the image
  regression suite's 38 scenes, PBR Lab with ray-traced GI and the clouds' shadows, and five labs under the Khronos
  validation layer (core and synchronization validation) on lavapipe:
  * the bloom's mips: a texture cleared at its first use and then used through its slices passed the clear's copy
    access to its slices' first transition each frame without the copy's stage (every frame with FellowEngine's bloom);
  * the terrain virtual texture's atlas: an sRGB view of an image created without that format among its views;
  * an upload by the transfer workers moved to the shaders' read layout with no destination access, so not made
    visible to them (stock Godot's code; the stock group of the suite too): a read-after-write hazard;
  * a MultiMesh's instances drawn from and then copied into the culling's pool: a barrier with the shaders' read as
    its source access and only a copy's stage as its source stages (the graph's code is stock Godot's).
* The suite and the labs after the fixes: no validation message but the layer's own (it cannot parse ray-query
  SPIR-V).
* The device loss of milestone 33 (ray-traced GI with the clouds' shadow map written and read in one frame): no hazard
  on the map on lavapipe with the new diagnostic `debug/cloud_shadow_same_frame`; the double buffer stays, and the RTX
  is asked again in milestone 143.

## 1. Checks

The image regression suite under validation (its README's command, new): 48 of 48 pictures identical, nothing but the
layer's own message. FellowBench's labs under validation on lavapipe (Slice Lab with ray-traced GI and the sky's
atmosphere and clouds, Rain, Water, Fire and Lights Lab): nothing but the layer's own message, and their pictures the
same bit for bit as the captures before the last fix — but Rain Lab's splashes, which differ as much between two runs
of one build.

## 2. Next

Milestone 143, the laptop's GPUs free again: m142 built on the laptop, then on the RTX Bistro's DDS textures streamed,
Crowd Lab with `update_lod`, Frontier Lab's levels of groups, and last milestone 33's device loss asked again
(Bistro exterior with ray-traced GI, the clouds' shadows and `debug/cloud_shadow_same_frame`, as in milestone 33); City
Lab on the Intel GPU (job f487) running.
