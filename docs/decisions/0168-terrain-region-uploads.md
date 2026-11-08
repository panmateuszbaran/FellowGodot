# 0168 — The terrain's edits upload only the texels they changed

Status: **KEEP, on** (exact: the same textures, read back bit for bit) · Branch `fellow/geometry` · Setting
`rendering/fellow/streaming/terrain_region_uploads` (on) · Code: `servers/rendering/rendering_device.{h,cpp}`
(`fellow_texture_update_region()`), `servers/rendering/rendering_server.h`, `rendering_server_default.h`,
`servers/rendering/storage/texture_storage.h`, `renderer_rd/storage_rd/texture_storage.{h,cpp}`
(`fellow_texture_2d_update_region()`), `modules/fellow_world/fellow_terrain_3d.{h,cpp}` (`_upload_texture()`, the
splat maps' edited rectangles) · Check: `benchmarks/project/bench/terrain_edit_check.gd`.

## Problem

Decision 0160's brushes update only the region they touch on the CPU — the heights, the normals, the splat weights and
the mips over them — but the RenderingServer updates a texture whole, so each frame with an edit uploaded the heights'
and normals' textures with all their mips (34 MB at 2049², 134 MB at 4097²) and an edited splat map's (22 and 89 MB).
On the RTX laptop (f360) that update was 10 ms at 2049² and 64.6 ms (heights and normals) plus 24.3 ms (a splat map)
at 4097²: 13-28 frames a second while sculpting or painting a 4 km map. Decision 0160 left it open: the RenderingServer
has no call to write part of a texture.

## Change

* `RenderingDevice::fellow_texture_update_region(texture, layer, mip, rect, data)`: one mip's rectangle of an
  uncompressed 2D texture, its rows packed, through the upload staging buffers in tiles of
  `texture_upload_region_size_px` — the stock `texture_update()`'s path (the same barriers and transfer graph), for a
  rectangle instead of every mip whole.
* `RenderingServer::fellow_texture_2d_update_region(texture, mip, rect, data)` (not bound; a no-op on renderers without
  a RenderingDevice): the RD texture storage passes it on for a 2D texture whose format the device takes as it is (the
  terrain's RF, RG8 and RGBA8 are), dropping the editor's cached image as a whole update does.
* `FellowTerrain3D` at a frame's end: the heights' region, the normals' region and a texel around (their central
  differences changed there), each splat map's edited rectangle (now kept like the heights') — each mip's rectangle
  over it, as `_terrain_mips_region()` wrote them (the same `>> 1` per level) — copied out of its image and uploaded.
  A region over a quarter of the texture, or a renderer without a RenderingDevice, updates the texture whole as before.

## Verification

`terrain_edit_check.gd` on lavapipe (513² heights with collision and foliage): every texture of the edited terrain —
heights, normals, both splat maps, all their mips, read back from the device — equals a terrain rebuilt from the edited
images bit for bit, and the undo's blits restore the originals bit for bit (19 of 19 checks; a whole update before).

The update at the frame's end on lavapipe (the editor build; the texture copies are CPU work there as on a GPU), the
setting off → on:

| | 2049² heights |
|---|---|
| the frame's update after a raise stroke (heights and normals) | 8.78 → **0.36 ms** |
| after a paint stroke (a splat map) | 4.62 → **0.03 ms** |
| a frame while a stroke is held (a dab a frame; lavapipe's frame, 320 × 180): raise; paint | 54.1 → 45.1 ms; 53.9 → 44.7 ms |

On the RTX the whole textures' upload was 10 ms (2049²) and 89 ms (4097², heights, normals and a splat map) of a
frame with an edit: what is uploaded now is the region's size, not the map's.

## Decision

**KEEP, on.** An edit costs the texels it changes, not the map: the same textures. Not done: compressed textures
(the terrain's are not); the collision shapes over the region are still rebuilt whole (one per 128 m region, 0.3-0.5 ms
headless).
