# 0038 — Light cluster tiles smaller than 32 pixels

Status: **REJECT** (diagnostic setting left in, stock 32 px by default) · Branch `fellow/geometry` · Diagnostics
`rendering/fellow/debug/cluster_size` (8, 16, 32, 64; read when the render buffers are set up) · Code:
`cluster_builder_rd.cpp` (`setup`).

## Idea

Godot's clustered lighting assigns lights, decals and reflection probes to screen tiles of 32 × 32 pixels and 32 depth
slices; every fragment walks its tile's elements, and the elements that do not reach it contribute nothing (with the
zero-attenuation skip, 0008, they are skipped). Smaller tiles would hand each fragment fewer elements to walk, at the
cost of more tiles to build. The shaders read the tile size from uniforms (scene shader, volumetric fog), so only the
cluster builder needed a setting.

## Measured (`fellow98`, `m8_f108_*`)

Images: lavapipe captures identical with 16 px tiles in Lights Lab, PBR Lab, City Lab, Foliage Lab and Blend Lab, but
**Stress Lab differs in 4 pixels (max |d| 31)** at the screen's right edge: the cluster builder rasterizes the
elements' proxy meshes at a quarter of the resolution (4× MSAA), and which tiles a proxy's samples land in decides
membership, so smaller tiles are not a strict subset of stock's. RTX sequences against 32 px: Lights Lab 90.9 dB
(16 px) / 94.1 dB (8 px), Bistro interior 67.1, PBR Lab 73.6, Sponza 75.5 dB — their floors.

Frame time between processes (3 runs, against 32 px):

| scene | 16 px | 8 px |
|---|---:|---:|
| Lights Lab (242 lights) | +2.3 % | +40.3 % |
| PBR Lab | +2.8 % (p 0.02) | +31.0 % |
| Bistro interior | −0.2 % | +16.2 % |
| City Lab | +0.5 % | +27.3 % |
| Bistro exterior | −1.0 % | −2.2 % |
| Sponza | +0.9 % | +0.9 % |

Per pass (`m8_f108_cluster_profile`, Lights Lab): the opaque pass saves 0.014 ms (16 px) / 0.060 ms (8 px), the
cluster's build costs 0.066 / 0.445 ms more. Walking the tile's elements is not where the opaque pass spends its time.

## Decision

**REJECT**: slower in every light-heavy scene and not exact (Stress Lab). The setting stays as a diagnostic.
