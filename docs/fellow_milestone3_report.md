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

| | Bistro exterior | Bistro interior |
|---|---:|---:|
| camera-pass triangles hidden behind solid geometry (Hi-Z, 2×2 texels, per stock instance / FellowEngine cluster) | 30 % (exact test 39 %) | 18 % (23 %) |
| frustum-only, smaller clusters (8192 / 2048 instead of 32768 / 4096) | −10 % (vs −5 %) | – |

Geometry costs about 0.5 ms of Bistro exterior's frame (coarser-LOD sizing): occlusion culling there is worth
~0.15 ms at most; not pursued for now.

## 3. Changes (decision records)

| | Decision | Toggle | Status |
|---|---|---|---|
| Light clusters with logarithmic depth slices | 0008 | `lights/cluster_depth_slices` | identity done (lavapipe bit-exact vs the complete element set; RTX); A/B running |
| Zero-attenuation light skip | 0008 | `lights/zero_attenuation_skip` | lavapipe identical; RTX pending (`fellow19`) |
| Shadow min/max pyramid v3 (RG16, incremental) | 0005 | `shadows/directional_max_mip` | identity done; A/B running (`fellow18`) |
| Pyramid v4: one fetch (overlapping windows), positional atlases | 0005 | `shadows/directional_max_mip`, `shadows/positional_max_mip` | lavapipe identical; RTX pending (`fellow19`) |
| Editor: headless `--import` no longer crashes on exit | (bug fix) | – | verified (stock crashes the same way: `f18_verify_b`) |

## 4. Mistakes (kept visible)

* The queued jobs overlapped from 10:29 to 11:05 (a queue marker that the queue script itself prints); every timing
  of that window is discarded and re-run (milestone-2 report §5 has the details).
