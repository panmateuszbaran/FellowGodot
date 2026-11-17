# 0264 — DDS textures streamed

Status: **KEEP** (with `rendering/fellow/streaming/textures`, decision 0146: off by default) · Branch `fellow/geometry` ·
Code: `modules/dds/texture_loader_dds.cpp` (the tail read, the stream source registered),
`modules/fellow_world/fellow_cell_proxies.cpp` (a streamed DDS texture read whole by the proxies' bake) · Checks:
`benchmarks/project/scenes/streaming_lab.gd` (`--streaming_dds=1`), `benchmarks/tools/lavapipe/` (Streaming Lab's
fixed cameras), the image regression suite.

## Problem

Texture streaming (decision 0146) streams a `CompressedTexture2D` — what Godot's importer makes of a PNG or a JPEG. A
`.dds` file is not imported: Godot loads it itself (`ResourceFormatDDS`), whole, as an `ImageTexture`, in the editor
and in an exported game. Content authored for engines that ship AAA games often comes as DDS — BC1, BC3, BC5 or BC7
with its authored mipmaps (no re-compression) — and Bistro, FellowBench's largest real scene, loads its 400-odd
textures that way: none streamed (milestone 45's limits: "Bistro's DDS textures (loaded as `ImageTexture`)"; the
readiness review: "DDS and uncompressed textures load whole").

## Change

A DDS file's mip chain is laid out as Godot's `Image` lays out its levels (each level's blocks one after the other,
mip 0 first) when its sides are whole blocks. With texture streaming on, `ResourceFormatDDS` loads a 2D texture (not
an array, a cube map or a volume) of a compressed format with its full chain and larger than `texture_resident_size`
as `CompressedTexture2D` does: it reads only its tail (the levels at or below 128 px) into the `ImageTexture`, keeps the
texture's full size (`set_size_override()`), and registers the file's position of its chain with the RenderingServer
(`texture_2d_fellow_set_stream_source()`); the streamer then reads the finer levels from the `.dds` as the cameras need
them, as for an imported texture. Every other DDS — without a chain, uncompressed, a cube map, sides not whole blocks,
an embedded one (glTF's) — loads as before.

## Verification

Streaming Lab on lavapipe (480 × 270, its four fixed cameras, a fixed 120 frames for the loads; the demote
diagnostics), its 48 materials' 2048² BC1 albedos and BC5 normal maps loaded as their imported `.ctex` files or as
DDS files written from them (`--streaming_dds=1`), texture streaming off and on:

| | textures loaded in | street start | street middle | façade close | aerial |
|---|---:|---:|---:|---:|---:|
| `.ctex`, streaming off (textures' video memory) | 450 ms | 513.3 MiB | 513.3 MiB | 513.3 MiB | 513.3 MiB |
| DDS, streaming off | 1 077 ms | 513.3 MiB | 513.3 MiB | 513.3 MiB | 513.3 MiB |
| `.ctex`, streamed (streamed resident of 384 MiB) | 22 ms | 142.8 (13.5) | 165.0 (35.7) | 172.2 (42.9) | 130.8 (1.5) |
| **DDS, streamed** | **11 ms** | **142.8 (13.5)** | **165.0 (35.7)** | **172.2 (42.9)** | **130.8 (1.5)** |

* The DDS files hold the imported textures' levels: off, the pictures the same as the `.ctex` ones bit for bit.
* Streamed, DDS as `.ctex`: the same residency at every camera, the same loads; the pictures within the streaming's
  own spread — DDS streamed against DDS whole 2 941 pixels of the four cameras a level apart, `.ctex` streamed against
  `.ctex` whole 2 969 pixels at most 2 levels (TAA's history while the loads land; the streamed DDS against the streamed
  `.ctex` 633 pixels, at most 2 levels).
* World Lab's cells textured with the DDS files, streamed, their proxies baked (`world_proxies_check.gd --dds=1
  --taa=0`, 8 × 8 cells): every lane bit for bit as with the `.ctex` files, the proxies' textures the same bytes — the
  bake read the DDS files whole.
* The image regression suite: exact (no scene of it loads a DDS file).

## Decision

Keep, with texture streaming: a DDS texture streams as an imported one. Bistro's measurement on the RTX follows when the
laptop's GPU is free.

## Rejected / not done

* **Importing DDS files into `.ctex`** (Godot's importer re-compresses what it imports): an authored BC7 or BC5 chain
  re-encoded, and a project's `.dds` references changed; the loader streams the file as it is.
* **`get_image()` of a streamed DDS texture** returns the levels resident (an `ImageTexture` keeps no path to read the
  rest from; `CompressedTexture2D` reads its file). The proxies' bake (decision 0251), which samples a texture's finer
  levels, reads a streamed DDS texture's file whole instead (`Image.load_dds_from_buffer()`); a game's own code that
  reads the image sees the resident levels.
* **Uncompressed and lossless textures** (PNG or WebP levels in a `.ctex`, uncompressed DDS): still loaded whole — 3D
  textures are imported VRAM-compressed by default.
