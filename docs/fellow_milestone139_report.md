# FellowEngine — milestone 139 report (DDS textures streamed)

Status: **FINAL** (2026-11-17). Branch `fellow/geometry`. Lavapipe in the cloud (the laptop's GPUs busy).

## 0. Result

* **DDS textures streamed** (decision 0264): with `rendering/fellow/streaming/textures`, a 2D `.dds` file of a
  compressed format (BC1-BC7) with its full mip chain — which Godot loads itself, not through the importer — reads only
  its tail and registers its file's chain with the RenderingServer, as an imported texture does: its finer levels are
  read as the cameras need them. Bistro's 400-odd DDS textures, which loaded whole, now stream (its measurement on the
  RTX when the laptop's GPU is free).
* Streaming Lab with its 48 materials as DDS files (384 MiB of textures): the same residency at each camera as the
  imported `.ctex` files (13.5-42.9 MiB at street level, 1.5 MiB from the air; the textures' video memory 131-172 MiB
  instead of 513 MiB), its textures loaded in 11 ms instead of 1 077 ms; the pictures within the streaming's own spread,
  and bit for bit those of the `.ctex` files with streaming off.
* The proxies' bake reads a streamed DDS texture's file whole (it samples the finer levels).

## 1. Checks

Streaming Lab `--streaming_dds=1` (DDS files written from its imported textures) on lavapipe's fixed cameras, streaming
off and on; `world_proxies_check.gd --dds=1` (the cells' textures as DDS files, streamed: the proxies baked from them);
the image regression suite (exact).

## 2. Next

Bistro streamed and Frontier Lab's levels of groups on the RTX; City Lab on the Intel GPU (job f487, queued) — when the
laptop's GPUs are free.
