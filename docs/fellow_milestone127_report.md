# FellowEngine — milestone 127 report (textured streaming proxies)

Status: **FINAL** (2026-11-16). Branch `fellow/geometry`. Without the GPU (the cloud's CPU and lavapipe).

## 0. Result

* `FellowWorldStreamer3D.bake_proxies()` bakes a texture for a cell's proxy where the cell's materials have albedo
  textures (decision 0251): the proxy unwrapped by xatlas with texels of `texel_size` (0.5 m; `max_texture_size` 512),
  each texel coloured by the cell's surface under it — material colour, vertex colours, the albedo texture at the level
  of the texel's size, past alpha-tested holes — BC1 with mipmaps in the proxy's file. The readiness assessment's
  "proxies without textures" is closed.
* A village of houses textured from one atlas (walls with windows, red roofs), 16 cells seen from 300 m: 50 % of the
  picture more than 16 levels off with decision 0174's proxies, 46 % with vertex colours on the new shape (every house
  kept, all in the atlas's mean colour), **11 %** with 0.5 m texels (29 KB a cell, BC1 with mipmaps; 0.98 s for 16
  cells).
* Proxies are simplified by meshoptimizer directly, welded by position, without locked borders or pruning: Godot's LOD
  generation, used before, had deleted nearly all of the village's houses to reach 5 %. Vertex-coloured proxies
  (`texel_size` 0, cells without textures, terrain regions) are coloured per corner by the surface under it.
* World Lab (12 × 12 cells of tiled-brick buildings, decision 0174's check): 1.64 → 0.43 % of the picture off (mean
  0.0043 → 0.0019) with textured proxies, 0.41 % / 0.0020 with vertex colours — the same at decision 0174's triangle
  count: the gain there is the shape and the corners' colours. The textures: 7.9 MiB for 144 cells, 10.5 s of bake.
* The editor's Bake Streaming Proxies dialog has the texel and reports the textured proxies' bytes.

## 1. Checks

`bench/proxy_texture_check.gd` (new: the village; A against vertex colours and 0.25, 0.5 and 1 m texels; a proxy's
file read again); `bench/world_proxies_check.gd` (`--texel`, `--flights=0`) with this milestone's textured and
vertex-coloured proxies and with decision 0174's; the editor check (its split level's boxes now textured: every proxy
file with its texture; `stream_back.gd` loading them in a game); the image regression suite (no proxies in its
scenes: every picture exact); the class reference.

## 2. Next

The proxies on the RTX at 1080p (their draw cost, the textures' memory in a large world); a proxy per group of far cells.
