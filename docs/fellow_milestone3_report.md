# FellowEngine — milestone 3 report

Status: **DRAFT** (work in progress). Base: Godot **4.7.2-stable**, FellowEngine configuration of milestone 2 as the
reference (`fellow16`-`fellow19`, every milestone-2 KEEP feature on). Machine and method: milestone-2 report.

## 1. Where the time goes after milestone 2 (sizing, `m3_sizing_profile`)

Ablation profile of the FellowEngine configuration (`fellow18`, day state, 1920×1080, one profiled run per variant,
per-pass GPU timestamps; ±0.05-0.1 ms run-to-run): each variant removes one thing, the delta is what it costs.

| removed | Lights Lab | Bistro interior | Bistro exterior | Sponza |
|---|---:|---:|---:|---:|
| (FellowEngine configuration, sum of passes) | 3.22 ms | 4.08 ms | 4.89 ms | 8.15 ms |
| all omni/spot lights | −1.80 | −1.22 | – (none) | – (none) |
| their shadows | −0.22 | **−0.82** | – | – |
| soft → hard positional shadows (8 PCF taps → 1) | −0.17 | **−0.59** | – | – |
| SSAO | **−0.61** | **−0.53** | **−0.51** | **−0.44** |
| SSIL | −0.02 | **−0.46** | **−0.42** | −0.20 |
| SSR | −0.08 | −0.21 | −0.16 | −0.19 |
| reflections (sky / probes) | −0.02 | −0.11 | −0.05 | −0.24 |
| decals and probes | −0.11 | +0.07 | +0.07 | +0.01 |
| coarser LOD (threshold 4) | −0.03 | −0.10 | −0.11 | **−0.71** |
| *light clusters with depth slices (0008)* | **−0.33** | +0.04 | +0.06 | +0.17 (noise) |

The scenes' positional lights: Lights Lab 242 (22 omni + 6 spot shadowed), Bistro interior 4 shadowed omni lights that
reach the whole room, Bistro exterior and Sponza none. Raw: `benchmarks/results/m3_sizing_profile*.{json,md}`.

Consequences:

1. **Positional shadow filtering** is the largest single cost in Bistro interior (0.6 ms of PCF taps for 4 lights):
   the min/max pyramid early-out of decision 0005 extended to the positional atlas (omni paraboloids, spots).
2. **SSAO + SSIL + SSR: 0.8-1.2 ms in every scene** — next, starting from the stage timings (gather, blur,
   interleave) that `fellow19` records.
3. **The light loop** where there are many lights: depth slices (0008) −0.33 ms of 1.8 ms in the Lights Lab.
4. Geometry remains significant only in Sponza (coarser LOD −0.7 ms; occlusion estimate below).

## 2. Offline estimates from the engine's own geometry (`--mode=geodump`)

| | Bistro exterior | Bistro interior | Sponza |
|---|---:|---:|---:|
| camera-pass triangles hidden behind solid geometry (Hi-Z, 2×2 texels, per stock instance / FellowEngine cluster) | 30 % (exact test 39 %) | 18 % (23 %) | 12 % (22 %) |
| frustum-only, smaller clusters (8192 / 2048 instead of 32768 / 4096) | −10 % (vs −5 %) | – | – |
| screen covered by sky (no geometry), flythrough mean | 2.7 % | 0.2 % | 0.5 % |

Geometry costs about 0.5 ms of Bistro exterior's frame and 0.7 ms of Sponza's (coarser-LOD sizing): occlusion
culling is worth ~0.15 ms at most in Bistro exterior and ~0.1 ms in Sponza (12 % of 0.7 ms); not pursued for now. The sky fraction rules out skipping SSAO/SSIL work on sky
pixels as a lever (an exact early-out, but for 0.2-2.7 % of the pixels).

## 3. Changes (decision records)

| | Decision | Toggle | Status |
|---|---|---|---|
| Light clusters with logarithmic depth slices | 0008 | `lights/cluster_depth_slices` | identity done (lavapipe bit-exact vs the complete element set; RTX); A/B: Lights Lab −0.27 ms (−7.9 %, p 0.002), other scenes within noise |
| Zero-attenuation light skip | 0008 | `lights/zero_attenuation_skip` | lavapipe identical; RTX pending (`fellow19`) |
| Shadow min/max pyramid v3 (RG16, incremental) | 0005 | `shadows/directional_max_mip` | identity done; A/B running (`fellow18`) |
| Pyramid v4: one fetch (overlapping windows), positional atlases | 0005 | `shadows/directional_max_mip`, `shadows/positional_max_mip` | lavapipe identical; RTX pending (`fellow19`) |
| Editor: headless `--import` no longer crashes on exit | (bug fix) | – | verified (stock crashes the same way: `f18_verify_b`) |
| Early submit: the frame's rendering no longer waits for the swap chain | 0009 | `frame/early_submit` | lavapipe identical (PBR Lab, Stress Lab); RTX identity, pacing diagnosis and A/B queued (`fellow20`) |

## 4. Screen-space effects, stage by stage (`m3_f19_profile`, `fellow19` defaults, GPU ms)

| | Lights Lab | Bistro interior | Bistro exterior | Sponza |
|---|---:|---:|---:|---:|
| SSAO gather / blur / interleave | 0.284 / 0.056 / 0.037 | 0.324 / 0.066 / 0.042 | 0.383 / 0.052 / 0.043 | 0.368 / 0.057 / 0.062 |
| SSIL gather / blur / interleave | – | 0.181 / 0.052 / 0.060 | 0.212 / 0.060 / 0.066 | 0.293 / 0.070 / 0.091 |
| SSR Hi-Z / trace / roughness filter / resolve | – | 0.025 / 0.026 / 0.043 / 0.078 | 0.030 / 0.035 / 0.055 / 0.113 | 0.026 / 0.194 / 0.045 / 0.101 |

The gathers dominate. The benchmark preset runs SSAO at *high* quality and full size (12 taps × 2 depth samples,
two gathers and five normal loads per pixel at 960×540 × 4 interleaved layers: ~64 M texture reads per frame) and
SSIL at medium, half size: these passes are bound by texture reads, as the algorithm is configured. What is exact
and left to take is small (sharing the neighbour normals and depths of a workgroup: ~15 % of the SSAO gather's reads).
The large levers here are the quality settings or a different algorithm, i.e. look changes with their own quality
evaluation — not taken silently.

## 5. The constant gap between frame time and GPU time

Every session since milestone 1 has frame time = viewport GPU time + 0.37-0.44 ms, whatever the scene, the CPU
recording time (0.2-1.5 ms) or the configuration, with 87-92 % GPU utilisation: 8-13 % of the frame rate at 3-5 ms
per frame. The hardware record of every session shows why it is worth chasing: the laptop's panel is driven by the
Intel iGPU, so every frame crosses from the RTX GPU to the iGPU and the compositor (hybrid graphics, the usual gaming
laptop). Stock Godot submits the whole frame after acquiring the swap chain image, and the whole submission waits on
the acquisition at the colour-output stage. Decision 0009 submits the rendering before the acquisition; the
`fellow20` job measures it together with two start-up settings that would also hide such a wait (a frame queue of
3, 4 swap chain images), using the runner's new per-variant start-up overrides.

## 6. Mistakes (kept visible)

* The queued jobs overlapped from 10:29 to 11:05 (a queue marker that the queue script itself prints); every timing
  of that window is discarded and re-run (milestone-2 report §5 has the details).
