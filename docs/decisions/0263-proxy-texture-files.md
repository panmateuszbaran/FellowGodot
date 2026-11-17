# 0263 — Proxies' textures in files of their own, streamed

Status: **KEEP** (`bake_proxies()`'s `texture_files`, on by default) · Branch `fellow/geometry` · Code:
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

`world_proxies_check.gd` (lavapipe, 640 × 360; World Lab's cells and regions with their proxies, groups and levels,
decision 0262) with the textures in files and inside the proxies, texture streaming off and on (`override.cfg`), each
lane's video memory of textures and, with streaming, the proxies' files as the streamer holds them. The editor check:
the files beside the proxies (and the editor's `.uid` files for them), each proxy's material reading its own. The image
regression suite (exact: no scene of it bakes proxies).

## Results

**Streaming off**: the files hold the same texels — the far views (the groups', the levels') the same pictures bit for
bit as with the textures inside the proxies; the near views within the runs' own spread (TAA's jitter: a few hundred
pixels a level apart from one run to the next).

**Streaming on**, the textures in files against inside the proxies (the proxies' textures whole):

| | the proxies' textures | resident | at their tails | textures' video memory | inside the proxies |
|---|---:|---:|---:|---:|---:|
| 8 × 8 cells, streamed view (C) | 69, 4.66 MB | 3.46 MB | 11 | 125.7 MB | 126.9 MB |
| 8 × 8 cells, from outside (F) | | 1.65 MB | 36 | 101.2 MB | 104.2 MB |
| 16 × 16 cells, streamed view (C) | 340, 24.2 MB | 15.6 MB | 45 | 144.1 MB | 152.7 MB |
| 16 × 16 cells, from outside (F) | | 6.9 MB | 256 | 112.6 MB | 129.9 MB |

From outside, 72 % of the proxies' textures not resident (17.3 MB of 24.2 MB) and the same picture; in the streamed
view a third (the proxies nearest the camera keep their finest levels: texture streaming's margins, decision 0146,
want a 512-texel proxy texture whole within a few hundred metres at this resolution). The cells' content's textures
stream as before (World Lab's Streaming Lab textures: 128 MB whole, 8.5 MB resident in C).

**The same pictures**: without TAA (`--taa=0`: its jitter's phase depends on how many frames the loads took), every
lane of the 8 × 8 cells — the world all loaded, streamed, with its proxies, its meshes streamed, from outside — is the
same bit for bit with the proxies' textures streamed from their files as with them whole inside the proxies (texture
streaming on in both; the proxies' textures 2.72 MB resident in C without TAA's margin).

## Decision

Keep, on by default: a world's proxies' textures, its largest group of textures not seen near, are streamed with the
rest; without texture streaming nothing changes but the files.

## Rejected / not done

* **Streaming the `ImageTexture`s themselves**: decision 0146's streamer reads a texture's levels from its file; a
  texture made in memory has none. A file per proxy is what an imported texture is.
* **One atlas file for all of a group's proxies**: one streamed texture for many proxies streams for the nearest of
  them; each proxy's own keeps each at its own distance.
