# 0146 — Texture streaming: only the mips the cameras can see

Status: **DRAFT** · Branch `fellow/geometry` · `servers/rendering/fellow_texture_streaming.{h,cpp}`,
`scene/resources/compressed_texture.cpp`, `renderer_scene_cull.cpp` (`fellow_texture_streaming_update`), RD storage
(`texture_2d_fellow_stream_swap`, `material_get_fellow_stream_info`, `mesh_surface_get_fellow_uv_density`) · Settings
`rendering/fellow/streaming/*` · Bench: `scenes/streaming_lab.gd` (`--scene streaming`),
`scripts/make_streaming_textures.py`, `tools/lavapipe/streaming_check.sh`.

## Problem

Stock Godot loads every texture whole: each level of each mip chain is resident from the moment its resource loads
until it is freed. A texture 200 m away that covers 30 pixels holds the same 2.7 MB (a 2048² BC1 map) as one a metre
from the camera. Sponza needed 2.5 GB of VRAM (milestone 3), most of it textures; an AAA level's textures do not fit in
8-12 GB that way, and the assessment (`docs/fellow_aaa_readiness.md`) put memory and streaming first among what an AAA
project would hit.

## Change

RESULTS_PENDING
