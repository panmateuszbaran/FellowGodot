# FellowEngine — milestone 118 report (compressed uploads staged as they are copied; cloth edges kept apart)

Status: **FINAL** (2026-11-15). Branch `fellow/geometry`. Milestone 117 turned decision 0225's staging trim off (a freed
memory block held the frame's submission up), leaving the transfer workers' staging buffers at 512 MiB after Sponza's
streamed flythrough; and left cloth edges able to pass through each other's.

## 0. Result

* **Staging for what is copied** (decision 0234, on): RenderingDevice reserved a block-compressed upload's staging as its
  rows of pixels — 4 times the data with 4x4 blocks — so a 21 MiB texture chain grew its worker's buffer to 128 MiB.
  Reserved as its rows of blocks, Sponza streamed ends with 160-192 MiB of staging instead of 512, the frames and
  pictures the same: decision 0225's saving without freeing anything during play.
* **Decision 0233 confirmed on an idle GPU** (job f454): the trim off, no frame over 29 ms in eight flythroughs; on, a
  45 ms frame, a 25 ms one with a 23 ms free, and a process that hung at exit — the trim's free task is gone, its frees
  now on the render thread.
* **Cloth edges kept off each other's** (decision 0235, off by default): a towel dropped across a bar of the same cloth
  between their particles stays 3 cm above it instead of passing through.

## 1. Compressed uploads staged as their rows of blocks (decision 0234)

`_texture_initialize()` reserved `pitch * height` a mip — the pitch a row of blocks' bytes, the height the mip's rows of
pixels — and copied `height / block height` rows of blocks into it; `texture_update()` did the same in the frame's
staging ring. `rendering/fellow/frame/transfer_tight_staging` (on, read when the device is created) reserves the rows of
blocks. A readback test (30 textures of six formats, created with data and updated, every mip) is byte for byte the same
either way; after a 4096 × 2048 BC7 texture the worker's staging is 16 MiB instead of 64. The regression suite with it on:
the same as its goldens.

The first laptop job's template never used it: the device is made before the RenderingServer registers FellowEngine's
settings, and the setting read at that moment was unset — it is read with defaults now (job f455 aborted, f456 again).

| Sponza streamed (job f456, 8 runs each) | staging at the end | the driver's memory at the end | GPU frame | p99.9 |
|---|---:|---:|---:|---:|
| stock's staging | 512 MiB | 2 853 MiB | 5.96-6.71 ms | 10.3-12.4 ms |
| tight staging | 160-192 MiB | 2 501-2 534 MiB | 5.99-6.63 ms | 10.6-12.5 ms |

Bistro exterior: staging 32 MiB either way, its load and frames the same.

## 2. Decision 0233 on an idle GPU

Job f454 (the GPU idle before and after it, every run's GPU frame 6.0-6.6 ms; eight runs each): the trim off, no frame
over 29 ms (the longest 12.9-20.0 ms, the 20 ms one waiting for the GPU); the trim on, a 25.0 ms frame with a 23.3 ms
free and a 45.4 ms frame, and one process hung at exit (killed after 15 minutes). The trim, for a game short of memory,
now frees on the render thread with the pool unlocked; no task of it is waited for at exit. A 256 MiB streaming budget's
evictions freed no memory block during play.

## 3. Cloth edges kept off each other's (decision 0235)

`FellowCloth3D.self_collision_edges`: once a frame, the pairs of edges of other layers nearer each other than twice the
distance (and not at rest) are found with their nearest points and the direction between them; in each substep a pair
nearer than the distance is pushed apart along it (the four particles by their inverse masses and places along the
edges), with decision 0229's friction. The pairs come from the particles nearer each other than the reach and the longest
edge now and not near each other along the cloth at rest (a Dijkstra over the rest lengths, once): a cloth that does not
come near itself tests nothing. Hashing every edge cost 64 flags 55 ms a frame of worker time; this way 17 ms.

| `cloth_check.gd`: a towel dropped across a bar of the same cloth | the crossing above the bar, at its lowest | at 4 s |
|---|---:|---:|
| particles and triangles | through | 7.47 m below |
| and edges | 3.06 cm | 3.13 cm |

The heap at 2.8 cm with the edges: no particle within half the distance of another layer's triangle (as without), 1 287
edge pairs, 3.0 ms against 1.4. The regression scenes `cloth` and `cape` exact.

## 4. Checks

The regression suite (36 scenes, the release template against the editor build, lavapipe) with tight staging on: the same
as its goldens. The class reference and the settings' descriptions regenerated; `make_rst.py` without errors or warnings.

## 5. Next

The download path's staging (`texture_get_data_async()`) as tight; the hitch sampler shown in the editor's debugger; the
GPU's own rare long frames (19-22 ms fence waits, about one a flythrough: the sampler cannot see into the GPU).
