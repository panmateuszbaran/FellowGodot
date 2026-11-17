# FellowEngine — milestone 138 report (proxies' textures streamed)

Status: **FINAL** (2026-11-17). Branch `fellow/geometry`. Lavapipe in the cloud (the laptop's GPUs busy).

## 0. Result

* **The proxies' textures in files of their own** (decision 0263): `bake_proxies()`'s `texture_files` (on) writes each
  textured proxy's, group's and level's texture beside it as `<proxy>_albedo.ctex` — the format of an imported
  VRAM-compressed texture — and the proxy's material reads it as a `CompressedTexture2D`. With texture streaming
  (`streaming/textures`, decision 0146) only the levels a camera needs are resident, as for any imported texture; the
  readiness review's "proxies' textures load with the level (not streamed)" is closed.
* World Lab's 16 × 16 cells (340 proxies' textures, 24.2 MB whole), texture streaming on: seen from outside **6.9 MB
  resident instead of 24.2 MB** (the textures' video memory 112.6 MB instead of 129.9 MB), the same picture; in the
  streamed view 15.6 MB (144.1 instead of 152.7 MB).
* The same pictures: without TAA every lane bit for bit the same as with the textures whole inside the proxies
  (streaming on in both); with streaming off the files hold the same texels.

## 1. Checks

`bench/world_proxies_check.gd` (`--texture_files`; with `streaming/textures` each lane's `proxy_textures`: the files as
texture streaming holds them; its streamed lanes wait for texture streaming's loads; `--taa=0`: every lane without TAA,
the same pictures from run to run); the editor check (the files beside the proxies, with the editor's `.uid` files, each
proxy's material reading its own); the image regression suite (exact).

## 2. Next

Frontier Lab with two levels of groups and its proxies' textures streamed, on the RTX; City Lab on the Intel GPU (job
f487, queued) — when the laptop's GPUs are free.
