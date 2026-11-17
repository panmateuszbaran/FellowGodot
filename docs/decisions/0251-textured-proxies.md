# 0251 — Streaming proxies with baked textures, and proxies that keep small buildings

Status: **KEEP** (`bake_proxies()`'s `texel_size`, 0.5 m by default; 0: vertex colours, on the new shape) · Branch
`fellow/geometry` · Code: `modules/fellow_world/fellow_cell_proxies.{h,cpp}` (the materials' samplers, the welded
simplification, the texture's bake, the corners' colours), `modules/fellow_world/fellow_world_streamer_3d.cpp`
(`bake_proxies()`'s `texel_size`, `max_texture_size` and texture totals),
`modules/fellow_editor/fellow_editor_plugin.{h,cpp}` (the dialog's texel) · Checks:
`benchmarks/project/bench/proxy_texture_check.gd` (new), `bench/world_proxies_check.gd` (`--texel`, `--flights`), the
editor check's proxies step.

## Problem

A streaming cell's proxy (decision 0174) is drawn in vertex colours: each material's mean colour. The readiness
assessment's streaming row: "proxies without textures". Decision 0174 had rejected textures because "a vertex colour
per a few metres is what the pixels show" — true of a tiled material, whose mean is its colour from afar, and wrong for
the way most buildings are textured: from atlases and trim sheets, one texture whose parts are the walls, the windows,
the roofs and the doors. The atlas's mean colour paints them all the same: red roofs and white walls become one
pinkish grey, and the windows are gone.

Measuring it found a second, worse problem. The proxies were simplified by Godot's LOD generation, which locks a mesh's
open borders (it is made for importing meshes in parts) and prunes small components. Buildings made of open patches —
walls not joined to the next wall at a corner, because their normals split the vertices there — cannot shrink with their
borders locked, so the generator reached its targets by deleting whole houses: a village's 16 proxies kept nearly none
of its 144 houses (45 triangles a cell of 4 770).

## Change

**The shape** (every proxy): the merged triangles welded by position alone and simplified to about `ratio` of them by
meshoptimizer directly — nothing pruned, no border locked but a terrain region's edges (its neighbours' proxies meet
them) — and the normals rebuilt from the faces, split where faces meet at more than 60°. The simplifier's error estimate (metres) bounds how far the source's surfaces can be from the
proxy's.

**The texture** (a cell with an albedo-textured material, in an editor build): the proxy is unwrapped by xatlas (as for
lightmaps) with texels of `texel_size` metres (0.5), coarser if the atlas would exceed `max_texture_size` (512). Each
texel takes the point and normal of the proxy under it and finds the source's outermost surface along the normal within
twice the simplifier's error (two texels at least; a BVH of the source's triangles). Its colour is that surface's
material's: albedo colour × vertex colours (when the material uses them) × the albedo texture at the interpolated UV
(with `uv1_scale` and `uv1_offset`), sampled bilinearly from the level whose texels match the proxy's texel (the
texture's levels decompressed once a bake, 512 texels at most), in linear space; an alpha-tested texel lets the search
go on behind it. Texels with no surface keep the proxy's vertex colour. Chart borders are rasterised a texel wide
(bilinear filtering reads them), empty texels take their neighbours' colours, and the texture gets mipmaps and BC1
compression (etcpak; a GPU without BC1 decompresses it on load), saved in the proxy's file with a material of its own
(roughness 0.9, anisotropic filtering). The texels are coloured on the worker threads, a row a task.

**Vertex colours** (`texel_size` 0, cells without textured materials, terrain regions, release templates: no
unwrapper): the same shape, each corner coloured by the source's surface a quarter of the way in from it, vertices split
where colours differ — a roof and the wall below it, welded at the eaves, keep their own colours.

**The editor**: Project > Tools > Bake Streaming Proxies has the texel (0.5 m; 0 for vertex colours) and reports the
textured proxies and their bytes.

## Verification

`proxy_texture_check.gd` (lavapipe, 960 × 540, a 40° view): a village of 4 × 4 cells of 64 m, nine houses a cell made
of 1 m quads whose walls, gables and roofs take their parts of one 512 × 512 atlas (white plaster with dark windows,
plain plaster, red tiles), on ground with a tiled grass texture; the sun without shadows (the proxies' colours alone).
Seen from 300 m and 600 m: every cell loaded (A), then every cell a proxy: vertex colours, and textures of 0.25, 0.5 and
1 m texels. The difference from A is over the pixels where either draws the village. A textured proxy's file is read
again: its texture BC1 with mipmaps. `world_proxies_check.gd` (decision 0174's World Lab: 12 × 12 cells of buildings
of 1.5 m quads and crates with tiled brick textures, on a terrain split into regions), lanes A-D without the flights,
with this milestone's textured and vertex-coloured proxies (`--texel`; `--ratio` 0.0344 for decision 0174's triangle
count) and with decision 0174's (an editor built without this change). The editor check's proxies step.
The image regression suite (no proxies in its scenes): every picture exact.

## Results

**The village** (16 cells of 4 770 triangles; a proxy 5 % of them):

| proxies | triangles a proxy | texture a proxy | bake, 16 cells | 300 m: pixels > 16 levels off A / mean | 600 m |
|---|---:|---:|---:|---:|---:|
| decision 0174 (vertex colours, Godot's LODs) | 45 (houses deleted) | — | 0.19 s | 50.0 % / 0.117 | 57.5 % / 0.121 |
| vertex colours, the new shape | 244 | — | 0.20 s | 46.1 % / 0.084 | 46.6 % / 0.074 |
| texels of 1 m | 244 | 9.7 KB | 0.46 s | 24.6 % / 0.045 | 16.8 % / 0.038 |
| **texels of 0.5 m** | 244 | 29.1 KB (208 × 220) | 0.98 s | **10.7 % / 0.028** | **16.4 % / 0.038** |
| texels of 0.25 m | 244 | 94.4 KB | 1.81 s | 12.9 % / 0.030 | 18.9 % / 0.041 |

96 % of the 0.5 m texels found a surface under them. The vertex-coloured proxies now keep every house, in the atlas's
mean colour; the textured ones show the red roofs, white walls and rows of windows (`a_loaded_300m.png` against
`d_texel_0_5_300m.png`). Texels finer than the pixels they cover add nothing (at 300 m a pixel is 0.4 m): 0.25 m is
not closer than 0.5 m.

**World Lab** (12 × 12 cells, 640 × 360; the proxies of the 135 unloaded cells in view, the regions' vertex-coloured):

| proxies of the cells | triangles (cells + regions) | mesh memory | their textures | bake, 144 cells | C: pixels > 16 levels off A / mean | D: C with streamed meshes |
|---|---:|---:|---:|---:|---:|---:|
| decision 0174's (vertex colours, Godot's LODs) | 58 378 + 9 264 | 22.0 MB | — | 5.0 s | 1.64 % / 0.0043 | 1.46 % / 0.0036 |
| vertex colours, the new shape | 84 629 + 9 180 | 24.5 MB | — | 4.4 s | **0.41 % / 0.0020** | 0.14 % / 0.0012 |
| the same at 3.4 % (as many triangles as before) | 58 149 + 9 180 | 23.4 MB | — | 4.0 s | 0.41 % / 0.0020 | 0.14 % / 0.0012 |
| **texels of 0.5 m** | 84 626 + 9 180 | 25.2 MB | 7.9 MiB (57 KB a cell) | 14.9 s (10.5 s the textures) | 0.43 % / **0.0019** | 0.16 % / **0.0011** |

(Every cell loaded, A: 2.45 M triangles; streamed to 160 m without proxies, B: 7.8 % / 0.051.) Here the materials are
tiled bricks, whose mean colour is what a far wall shows: the texture changes little (the mean difference 5 % lower),
and the gain — four times closer to A than decision 0174's proxies, at the same triangle count — is the shape and the
corners' colours. 87 % of the texels found a surface under them (the crates' proxies lose most of their shape). The
regions' proxies keep their edges (9 180 triangles; 3 550 without the lock, their edges free to move apart).

**The editor check**: its split level's boxes, now with a gradient texture, give 15 textured proxies (6.7 KiB in all),
saved with the level and shown by a game while their cells are unloaded. The image regression suite: 38 scenes exact.

**On the laptop** (milestone 131, job f464-f465: RTX 5070 Ti Laptop, 1080p; textured proxies need the editor build's
UV unwrapper): the village's pictures as on lavapipe — vertex colours 46 % of the picture more than 16 levels off at
300 m, 0.5 m texels 10.4 %, 0.25 m 13.0 %, 1 m 24.1 % — and its 16 cells baked in 0.51 s (0.32 s the textures). World
Lab's 144 cells: 8.1 s, 4.2-4.3 s of it the textures. Frontier Lab's 4 km world (1 024 cells, 31.4 M triangles): 272
of its 999 proxies textured (its towns'), 20 MB of textures, its cells' proxies baked in 140 s (29 s the textures).

## Decision

Keep, textured by default: an unloaded cell's proxy now looks like the cell — its atlases' and trim sheets' parts in
their own colours — for 30-60 KB a cell and three times the bake, and every proxy keeps its small buildings. A world
textured only with tiled materials loses little with `texel_size` 0 (no texture memory, a third of the bake).

## Rejected / not done

* **A fixed texture size a cell** (as HLOD's impostor atlases): a dense block and an empty field get the same texels; a
  texel size keeps the density even across cells, `max_texture_size` bounding the largest.
* **Impostors** (octahedral views, as HLOD's): a cell is seen from all around and from above; a proxy's texture is its
  surface's colour, lit by the scene.
* **Normal and roughness maps for proxies**: from beyond the streamer's distance a texel is a pixel or less; the albedo
  carried the difference here.
* **Streaming the proxies' textures**: they load with the level (16 cells: 0.47 MB; 2 500 cells of this village would be
  73 MB at 0.5 m, 24 MB at 1 m); texture streaming (decision 0146) streams imported textures' files, not images
  embedded in a resource.
* **Baking at run time** (templates have no UV unwrapper): proxies are baked in the editor.
* **Not yet**: triplanar materials, `ShaderMaterial`s and detail textures contribute their mean colour; the gutter
  between charts is one texel, so a proxy's smallest mips mix neighbouring charts; a proxy's texture is not shared by
  cells with the same content.
