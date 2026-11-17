# 0267 — Vulkan validation across the regression suite and the labs: four invalid synchronizations fixed

Status: **KEEP** (correctness fixes: the same pictures; two in stock Godot's code) · Branch `fellow/geometry` · Code:
`servers/rendering/rendering_device_graph.cpp` (a barrier's source stages include its transfer accesses' stages; a
buffer's barrier carries only its last usage's writes as source accesses),
`servers/rendering/rendering_device.cpp` (an upload's transition visible to the shaders' reads),
`modules/fellow_world/fellow_terrain_vt.cpp` (the virtual texture's atlas allows its sRGB view),
`servers/rendering/renderer_rd/effects/fellow_clouds.cpp` and `fellow_settings.cpp`
(`debug/cloud_shadow_same_frame`, diagnostics) · Checks: the image regression suite under the Khronos validation layer
(core and synchronization validation, lavapipe), the labs' captures under it (Slice Lab with ray-traced GI, the sky's
atmosphere and clouds; Rain, Water, Fire and Lights Lab).

## Problem

FellowEngine's synchronization was last swept with Vulkan's synchronization validation in milestone 13 (six labs, no
hazard). A hundred and thirty milestones of compute passes, mip chains, streamed and uploaded textures, ray-traced
effects and virtual textures later, the frame's barriers had not been looked at by the validation layer as a whole. And
one device loss on the RTX (milestone 33: ray-traced GI with the clouds' shadow map written and read in one frame) was
avoided by double-buffering the map without its cause being found (decision 0133).

## What the sweep found

The image regression suite's 38 scenes (all four groups: FellowEngine's features, the render thread, stock with
FellowEngine off), PBR Lab with ray-traced GI and the clouds' shadows, and the captures of five labs (Slice Lab with
ray-traced GI, the sky's atmosphere and clouds; Rain, Water, Fire and Lights Lab), run on lavapipe with the Khronos
validation layer (core and synchronization validation) and Godot's `--gpu-validation`:

* **A copy's access without the copy's stage** (`VUID-vkCmdPipelineBarrier-srcAccessMask-02815`, every frame with
  FellowEngine's bloom): a texture cleared at its first use (decision 0070) and then used only through its slices (the
  bloom's mips) kept the clear's copy access in its tracker; each frame's first transition of its slices from the
  parent's state carried that copy-write access with only the compute stage of the commands that used them as source
  stages — invalid usage, and on a GPU the copy's writes unordered by the barrier. The graph now adds the stages of a
  barrier's transfer accesses (copy, resolve, clear) to its source stages.
* **An sRGB view of an image that does not allow one** (`VUID-VkImageViewCreateInfo-image-01762`, the terrain virtual
  texture's scene): a `Texture2DRD` makes an sRGB view of an RGBA8 texture, and the virtual texture's atlas was created
  without listing it (Vulkan creates the image with a mutable format only when the formats to share are given). The
  atlas now lists its UNORM and sRGB formats.
* **An upload not made visible to the shaders that read it** (`SYNC-HAZARD-READ-AFTER-WRITE`, the lights scene — in
  the stock group too: stock Godot's code): a texture uploaded by the transfer workers is moved to the shaders' read
  layout by a barrier at the start of the frame's command buffer, whose destination access stock left empty; a fragment
  shader's sample of it then reads after the layout transition without its writes made visible to it. The barrier's
  destination access is now the shaders' reads.
* **A shader's read as a source access without a shader stage** (`VUID-vkCmdPipelineBarrier-srcAccessMask-02815`, Slice
  Lab's first frames; the graph's code is stock Godot's): a MultiMesh's instance buffer, drawn from and then copied
  into the MultiMesh culling's pool (decision 0034), moves from one read to another; the graph orders a read after a
  read after nothing but the buffer's last write, so the barrier's source stages were that write's (the upload's copy)
  and its source access the shaders' read, which only shader stages support. A read in a barrier's source makes nothing
  available (only writes are): a buffer's barrier now carries only its last usage's write accesses as source accesses,
  which a GPU obeying the specification treats as before.

Each fixed, the sweep again: no validation message but the layer's own (`VUID-VkShaderModuleCreateInfo-pCode-08739`:
this layer cannot parse ray-query SPIR-V, which lavapipe runs), and every picture of the suite and the labs the same
bit for bit.

**The device loss of milestone 33**: with `debug/cloud_shadow_same_frame` (new, diagnostics: the colour passes read the
map written in the same frame, as before decision 0133), PBR Lab with ray-traced GI and the clouds' shadows reports no
hazard on the map; the cause on NVIDIA's driver remains unfound, and the double buffer stays. With the invalid barrier
above fixed (the frame's bloom, there in milestone 33's captures too), the RTX can be asked again — PBR Lab with
ray-traced GI, the clouds' shadows and `debug/cloud_shadow_same_frame` — when its GPU is free (not in this milestone).

## Decision

Keep: four invalid synchronizations gone, the same pictures. The sweep belongs with the regression suite's run after
changes to passes and barriers (its command in the suite's README).

## Rejected / not done

* **Fixing stock's sRGB view at its source** (`texture_rd_initialize()` making an sRGB view of any RGBA8 texture whether
  its image allows it or not): a `Texture2DRD`'s maker is the one that knows the formats it will be read in;
  FellowEngine's one such texture lists them.
