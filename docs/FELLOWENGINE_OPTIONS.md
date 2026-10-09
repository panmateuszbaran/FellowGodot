# FellowEngine options

All FellowEngine behaviour sits under **Project Settings → Rendering → Fellow** (`rendering/fellow/*`). There are two
kinds of options:

* **Exact** options do not change a single pixel compared with Godot 4.7.2 (verified on lavapipe bit for bit and on
  RTX at the run-to-run floor). They are on by default when they were measured faster, and autotune
  (`autotune/enabled`) switches the scene-dependent ones per scene.
* **Visually lossless** options change the image within measured limits (PSNR against the stock image on still
  captures and on moving camera sequences, the largest per-pixel difference, the share of pixels off by more than two
  levels). They are **off by default**; a project turns them on deliberately.

Every option has a decision record in `docs/decisions/` with its measurements.

## Visually lossless options (off by default)

| setting | what it does | where it pays | image cost | record |
|---|---|---|---|---|
| `shading/adaptive_vrs` (start-up) | Variable rate shading per 16 × 16 tile, chosen from the previous frame's image and reprojected with the current depth; glossy surfaces, discarding and depth-writing materials keep full rate | scenes where much of the screen is smooth or dark (lights, flat walls, sky-lit interiors) | (f89/f90) | 0027 |
| `postfx/ssao_temporal` | SSAO takes half its taps per frame, the other half from the previous frame (reprojected, depth-checked) | every scene with SSAO (low to high quality, full or half size) | (f89) | 0028 |
| `postfx/ssil_temporal` | the same for SSIL | scenes with SSIL | (f89) | 0030 |
| `postfx/fog_half_rate` | volumetric fog lights half its froxels per frame (checkerboard), the rest is reprojected | scenes with volumetric fog and temporal reprojection | (f89) | 0031 |

`shading/adaptive_vrs_threshold` (0.006, luma 0-1) is the per-tile error budget; 0 keeps the attachment with every
tile at full rate. `shading/adaptive_vrs_max_rate` limits the coarsest rate (2 × 2 or 4 × 4).
`shading/adaptive_vrs_refresh` shades every tile at full rate one frame in N so that detail lost to coarse shading is
seen again. `shading/adaptive_vrs_gloss_guard` keeps glossy, curved surfaces at full rate. Adaptive shading needs a
GPU with fragment shading rate attachments (Vulkan `VK_KHR_fragment_shading_rate`), a single view, the depth pre-pass
and no MSAA; otherwise every tile is shaded at full rate.

## Exact options (on by default unless noted)

| area | settings | record |
|---|---|---|
| Shadows | `shadows/directional_cache`, `shadows/skip_*`, `shadows/directional_max_mip`, `shadows/positional_max_mip` | 0004, 0005 |
| Geometry | `geometry/mesh_clusters` (frustum culling inside large static surfaces) | 0007 |
| Lights | `lights/cluster_depth_slices`, `lights/zero_attenuation_skip`, `lights/cluster_pack_parallel` | 0008, 0014 |
| Shading | `shading/blended_alpha_zero_skip` | 0010 |
| Post | `postfx/fast_luminance_reduce`, `postfx/taa_history_pingpong` | 0025, 0002 |
| Render thread | `cpu/parallel_*`, `cpu/batched_shadow_lists`, `cpu/compact_shadow_instances`, `cpu/persistent_instances` | 0016, 0018, 0022 |
| Frame | `frame/parallel_replay`, `frame/present_thread`, `frame/submit_thread`, `frame/async_compute` (NVIDIA) | 0015, 0017 |
| Autotune | `autotune/enabled` (chooses the scene-dependent exact options by measurement) | 0020 |

`postfx/taa_resolve_v2` (on) is a look change from milestone 1 (TAA with reverse-Z dilation and YCoCg variance
clipping, decision 0001).

## Measured and rejected (left in, off)

Occlusion culling of the opaque pass (0026), the depth pre-pass in two orders (0029), SSAO/SSIL coherent gathers
(0024), TAA in 16 × 16 groups (0023), instance buffers in system memory (0021), early submit (0009), TAA camera motion
vectors (0013), shared-memory SSAO tiles (0011). Not merged: soft shadows with half their taps under TAA (0032).
