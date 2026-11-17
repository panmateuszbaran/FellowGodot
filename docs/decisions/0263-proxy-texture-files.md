# 0263 — Proxies' textures in files of their own, streamed

Status: **IN PROGRESS** (`bake_proxies()`'s `texture_files`, on by default) · Branch `fellow/geometry` · Code:
`modules/fellow_world/fellow_cell_proxies.{h,cpp}` (`move_texture_to_file()`), `modules/fellow_world/fellow_world_streamer_3d.cpp`
(each textured proxy's, group's and level's texture), `modules/fellow_editor/fellow_editor_plugin.cpp` (the dialog's
report) · Checks: `benchmarks/project/bench/world_proxies_check.gd` (`--texture_files`; with `streaming/textures` each
lane's `proxy_textures`), the editor check (the files beside the proxies, the proxies' materials reading them).

## Problem

A textured proxy (decision 0251) keeps its texture — BC1, its mipmaps — inside its mesh's file, an `ImageTexture`: it
loads whole with the level, every level of it resident for as long as the level is, whether its cell is loaded (the
proxy hidden), near or kilometres away. Texture streaming (decision 0146, `rendering/fellow/streaming/textures`) keeps
only the levels a camera needs of every `CompressedTexture2D` file, not of textures made in memory. The readiness
review's streaming row: "proxies' textures load with the level (not streamed)". A world's proxies are thousands of
textures: World Lab's 16 × 16 cells' 25 MB, a 16 km world's hundreds of MB, nearly all of it for proxies a few pixels
tall or hidden behind their loaded cells.

## Change

* **`bake_proxies()`'s `texture_files`** (on): each textured proxy's texture — the cells', the groups' and the levels'
  (decision 0262) — is written beside it as `<proxy>_albedo.ctex`, the format Godot's texture importer writes for a
  VRAM-compressed texture (its chain from the finest level down as one image), loaded back as a `CompressedTexture2D`
  and set on the proxy's material before the proxy is saved: the proxy's file refers to it.
* **With texture streaming** its loads only its tail (the levels at or below `texture_resident_size`, 128 px), its
  finer levels read while a camera needs them (the proxy's UV density, its distance) and released after — as any
  imported texture. Without it the file loads whole: the same texels, the same pictures as before.
* A coarser level's bake reads the textures below it from their files (`get_image()`: the whole chain from the file
  when streamed). The bake reports `texture_files` and `texture_file_bytes`; the editor's dialog says how many.

## Verification

TODO

## Decision

TODO

## Rejected / not done

* **Streaming the `ImageTexture`s themselves**: decision 0146's streamer reads a texture's levels from its file; a
  texture made in memory has none. A file per proxy is what an imported texture is.
* **One atlas file for all of a group's proxies**: one streamed texture for many proxies streams for the nearest of
  them; each proxy's own keeps each at its own distance.
