# 0080 — Ray tracing foundation: ray queries, acceleration structures for the meshes near the camera

Status: **KEEP**, off by default (the base of the ray-traced effects 0081 and 0082) · Branch `fellow/geometry` ·
Settings `rendering/fellow/raytracing/enabled` (start-up), `radius`, `skinned`; debug `debug/raytracing_view`,
`debug/raytracing_decode_positions` ·
Code: `drivers/vulkan/rendering_device_driver_vulkan.*` (ray query feature, vertex format query),
`servers/rendering/rendering_device.*`, `rendering_device_driver.h`, `renderer_rd/storage_rd/mesh_storage.*` (buffers,
bottom-level structures), `renderer_rd/shaders/fellow_blas_decode.glsl`, `renderer_scene_render.h`,
`renderer_scene_cull.cpp` (the instances near the camera), `renderer_rd/forward_clustered/render_forward_clustered.*`
(the top-level structure), `fellow_settings.*`.

## Problem

Milestone 16 asks for ray-traced and path-traced lighting at a moderate cost. Godot 4.7 ships the RenderingDevice
side of hardware ray tracing (bottom- and top-level acceleration structures, ray tracing pipelines), but nothing in
the renderer uses it, and three gaps keep it from being used by effects:

* **Ray queries are never enabled.** The Vulkan driver declares `ray_query_support` and never sets it: the extension
  (`VK_KHR_ray_query`) is not requested and its feature not enabled, so compute and fragment shaders cannot trace
  (`SUPPORTS_RAY_QUERY` is false on every device). Screen-space effects (AO, shadows, reflections) are written as compute
  passes; a ray tracing pipeline per effect would be heavier to write and to run.
* **A release build cannot build a structure from indexed geometry.** `blas_create()` refuses an index buffer whose
  largest index is not below the vertex count; that largest index is only computed in debug builds (and only for
  buffers created with data) and is 0xFFFFFFFF otherwise, so every indexed geometry is refused in `template_release`.
* **Mesh buffers are not acceleration structure inputs.** Vertex and index buffers are created without device addresses
  and without the build-input usage.

## Change

* **Ray queries.** The driver requests `VK_KHR_ray_query` (optional, like the ray tracing pipeline extension), reads
  `rayQuery` with the other features and enables it at device creation; `SUPPORTS_RAY_QUERY` then reports the device.
  Shaders tracing rays use `#version 460` and `GL_EXT_ray_query` (all shaders are already SPIR-V 1.4). The SPIR-V
  optimizer after specialization (re-spirv) does not know `OpTypeRayQueryKHR` and keeps those shaders as compiled.
* **The index check** is skipped where the largest index is unknown (0xFFFFFFFF), as for buffers without data.
* **`raytracing/enabled`** (start-up, off): where the device has ray queries and buffer device addresses
  (`Fellow::raytracing_active()`, decided once), mesh vertex and index buffers and the skinning output buffers are created
  with device addresses and the build-input usage. Nothing else changes until an effect asks for the scene.
* **Bottom-level structures** (`MeshStorage::fellow_mesh_surface_get_blas()`): one per mesh surface (triangles only),
  built on first use from the surface's own buffers — positions first in the vertex buffer: three floats, or for
  compressed surfaces four UNORM16 relative to the surface's box (the instance transform then includes the box, as the
  scene shader decodes). Where the device cannot build from `R16G16B16A16_UNORM` (asked through a new driver query,
  `fellow_acceleration_structure_vertex_format_supported()`), a compute pass decodes the positions once into a float
  buffer (`fellow_blas_decode.glsl`; forced with `debug/raytracing_decode_positions`). Rebuilt after
  `mesh_surface_update_vertex_region()`; freed with the surface (and with a replaced index buffer, mesh clusters).
  Skinned and blend shape surfaces (`raytracing/skinned`, on) have one structure per output buffer, rebuilt when the
  skinning wrote it since its build (fast build).
* **The scene.** When an effect is on (`Fellow::raytracing_scene_wanted()`), the culler gathers, just before rendering a
  camera (not reflection probes), the visible mesh instances whose bounds meet the box of `raytracing/radius` (200 m)
  around the camera and in its layers, with a mask: bit 0 drawn by the camera (not "shadows only"), bit 1 casts
  shadows. The Forward+ renderer then builds the frame's top-level structure (per view's render buffers, recreated when
  it needs more instances): an instance per surface that is drawn opaque (or casts shadows), each surface once. Facing
  as the raster pass culls: double-sided materials are never culled, front-culling materials and mirrored transforms
  are flipped (Godot's front faces are clockwise from outside, the ray tracing convention counter-clockwise); a ray that
  culls back faces passes out of a closed mesh as the camera sees out of it. Ray queries ignore the hit shader binding
  table, but the device's build asks for a range: offset 0, one group.
* **Debug** (`debug/raytracing_view`, with the viewport's SSAO debug draw and RTAO on): the scene seen from the camera,
  1 = hit distances, 2 = where the hit matches the depth buffer.

## Verification

lavapipe (Mesa 25.2.8: ray queries, acceleration structures):

* **Off, and on without an effect: the image is unchanged** (City Lab, Crowd Lab captures against the defaults: see
  the results below).
* **The scene matches the raster image.** `debug/raytracing_view` 2 (a primary ray per pixel against the depth buffer's
  distance): City Lab street 99.71 %, roofs 98.38 % of pixels agree; aerial 86.6 % — the rest is the city beyond the
  200 m radius, as it should be. Without the facing rule the street camera, which the benchmark places inside a
  building (raster culling hides it), saw the building's inside walls.

## Decision

**KEEP, off by default.** The base the ray-traced effects build on; with it off nothing changes.
