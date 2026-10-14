# FellowEngine — milestone 10 report (stock's inefficiencies: the cluster build, glow's blur)

Status: **FINAL** (`fellow128`-`fellow131`, measured 2026-10-14). Branch
`fellow/geometry`. Benchmark machine as before (Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop GPU,
NVIDIA GPU only, Windows 11, 1920×1080, "high" preset). Decision records `docs/decisions/0051`, `0052`; user-facing
summary `docs/FELLOWENGINE_OPTIONS.md`.

## 0. Result

Frame time, `m10_f152_final` (3 interleaved runs per build and scene, 20 s each after a 30 s heat soak; paired
statistics): stock Godot 4.7.2, milestone 9's final build (`fellow127`), this milestone's (`fellow131`) with its
defaults, and with the visually lossless options (`postfx/ssao_temporal`, `postfx/ssil_temporal`,
`postfx/fog_half_rate`, `shading/adaptive_vrs`).

| scene | stock ms | m9 defaults | **m10 defaults** | m10 vs stock | m10 vs m9 (pairs faster) | m10 lossless | vs stock |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sponza | 13.715 | 7.032 | 6.766 | **−50.7 %** | −3.8 % (3/3; noisy, CV 3-5 %) | 6.513 | −52.5 % |
| Bistro exterior | 6.754 | 4.540 | 4.451 | **−34.1 %** | −2.0 % (3/3, p 0.06) | 4.315 | −36.1 % |
| City Lab | 2.954 | 2.085 | 2.070 | **−29.9 %** | −0.7 % (3/3, n.s.) | 2.102 | −28.9 % |
| Lights Lab | 3.044 | 2.573 | 2.352 | **−22.7 %** | **−8.6 %** (3/3, p 0.05) | 1.860 | −38.9 % |
| Foliage Lab | 3.652 | 3.138 | 3.015 | **−17.5 %** | −3.9 % (3/3, p 0.08) | 3.035 | −16.9 % |
| Bistro interior | 4.183 | 3.508 | 3.458 | **−17.3 %** | −1.4 % (3/3, p 0.12) | 3.148 | −24.7 % |
| PBR Lab | 2.558 | 2.350 | 2.293 | **−10.4 %** | −2.4 % (2/3; noisy, one slow run) | 2.034 | −20.5 % |
| Stress Lab | 4.928 | 4.742 | 4.776 | −3.1 % (p 0.07) | +0.7 % (1/3, n.s.) | 4.478 | −9.1 % |

Foliage Lab with its vegetation in one MultiMesh per type (`m10_f152_foliage_single`): stock 10.750 ms, `fellow131`
5.018 ms (**−53.3 %**, 3/3 pairs).

Every scene except Stress Lab (a robustness scene, bound by its scripted mesh updates) is faster than with milestone 9's
defaults in every pair of runs; the in-process A/Bs of the two changes (§2, §3) give −1.1 to −6.1 % of GPU time per
scene. Images, `fellow130`'s defaults against `fellow127`'s (`m10_f149_defaults_temporal`, 60-frame sequences): Bistro
exterior, City Lab and Stress Lab bit for bit; Bistro interior 67.1, Foliage Lab 78.2, Lights Lab 94.2, PBR Lab 76.3,
Sponza 75.5 dB, their run-to-run floors. (`fellow131` differs from `fellow130` by the SSR filter option, off.)

New in this milestone and on by default, both exact: **the light cluster build without its 21.5 MB clear per frame,
with instanced proxy draws and a pack pass per element** (0051), and **glow's blur without shared-memory bank
conflicts, in 16 × 16 tiles** (0052). The same padding in SSR's roughness filter was measured and rejected (no gain).

## 1. The question

After nine milestones of FellowEngine's own passes, this one went back to stock passes that every scene runs and asked
what they spend their time on, rather than what they compute: the light cluster build (0.22 ms of Lights Lab's frame
plus a clear that no profiler region reports) and glow (0.14-0.20 ms in every scene).

## 2. The light cluster build (0051, exact, on)

Forward+ rebuilds its light, decal and probe clusters every frame:

* **a 21.5 MB clear**: the render buffer holds a depth word per tile for each of the 2 560 possible elements
  (`max_clustered_elements` 512 × 5 types), cleared every frame whatever the number of elements;
* **one draw per element** of its proxy mesh (stock's instancing is disabled: its fragment shader merges a subgroup's
  invocations by tile only, which is correct only with one element per subgroup);
* **a pack pass** that walks each tile's elements one after the other (0014 gave every depth slice an invocation, but
  each still walks every element).

Now the pack pass gives every element of a chunk of 64 its own invocation (one round of loads per chunk instead of per
element), collects (type, slice range, index) in shared memory, then gives every slice an invocation over that list.
It reads every word of the render buffer that the render pass sets, and zeroes it: the buffer is left all zero, and
the clear is skipped in the next frame. Consecutive elements with the same proxy mesh are one instanced draw; the
fragment shader groups its subgroup by tile and element. Elements whose depth slices come from their conservative range
(FellowEngine's depth slices, 0008) are drawn without depth words, which the pack pass reads only to know that the
element reached the tile (its bit says so).

* `debug/cluster_verify`: the build repeated with stock's draws and 0014's pack every frame, both read back and
  compared word for word, and the render buffer checked to be all zero: **0 mismatches** in Lights Lab, Bistro
  interior, City Lab and PBR Lab on the RTX (10.6-21.1 billion words each), and on lavapipe; the negative control (a
  deliberate error) 5.8 million in Lights Lab.
* Images, on against off: City Lab bit for bit, the other sequences at their floors.
* In one process: GPU **−2.3 %** Lights Lab (the pack 0.093 → 0.039 ms, the render pass 0.102 → 0.075 with v2),
  **−1.1 %** PBR Lab (the clear: 0.025 ms that no region shows), −0.9 % City Lab; Bistro interior −0.1 % (n.s.; its
  async compute hides the clear). Instancing saves render-thread time more than GPU time (Lights Lab −3.0 %).

## 3. Glow's blur (0052, exact, on)

The Gaussian blur that builds glow's levels (`copy.glsl`) stores its horizontal pass's results by column with a stride
of 16 vec4. A quarter warp (eight invocations, eight columns of one row) then accesses entries 16 vec4 = 64 words apart:
all in the same four of shared memory's 32 banks. Every access of the vertical pass is an 8-way bank conflict, about
three quarters of the shared-memory traffic of the shader. A stride of 17 removes the conflicts; 16 × 16 tiles
(256 invocations, 24 × 24 samples, stride 25) also take 2.25 bilinear samples per output instead of 4. Every sample is
taken where stock takes it and the sums are stock's, in stock's order.

* Images: lavapipe identical (Lights Lab, PBR Lab captures); RTX Bistro exterior and City Lab bit for bit, the others at
  their floors.
* In one process, glow's region **0.136-0.199 → 0.053-0.076 ms**; GPU City Lab **−4.5 %**, Lights Lab −3.8 %, PBR Lab
  −3.8 %, Bistro interior −2.0 %, Bistro exterior −1.7 %, Sponza −1.1 %.

SSR's roughness filter has the same layout (both passes conflicted): padded, exact, and no faster (its mips are small;
the time is the dispatches). Rejected, left in off (`postfx/ssr_filter_padded`).

## 4. Milestone 9's PBR Lab regression, and light tile culling's cost in Bistro interior

* `m9_f145` had measured milestone 9's defaults 2.6 % slower than milestone 8's in PBR Lab. In one process neither of
  the milestone's changes explains it (`m10_f148`: `lights/fields_early` off +0.0 %, `lights/tile_cull` off −0.5 %, its
  autotune evaluations); between processes, nine builds from `fellow110` to `fellow130` differ from `fellow110` by
  −1.4 to +1.8 % with no step at any change (`m10_f150`, 3 runs each): build-to-build variation of about ±2 %.
  `fellow130` is 1.7 % faster than `fellow110` there (3/3 pairs).
* 0049's unexplained cost of light tile culling in Bistro interior (every pass a few per cent slower, `m9_f138`-`f140`)
  did not recur (`m10_f148`): −0.4 % with async compute (the opaque pass −0.08 ms), +1.3 % without (the culling pass
  itself, the opaque pass unchanged). Autotune measures it per scene either way.

## 5. Tooling

* `debug/cluster_verify` (1, 2 with a negative control), `debug/cluster_keep_depth_words`; stats
  `lights_cluster_draws`, `lights_cluster_elements`, `lights_cluster_verify_words`, `lights_cluster_verify_mismatches`.
* `ab_inproc.py`: a metric a run did not record (all zero) no longer stops the report.

## 6. Incidents

* None in the measurements. A job's in-process report failed on a scene that records no GPU time with autotune on
  (Stress Lab, `m10_f148_def_ab`): the tool divided by zero; fixed, and the report regenerated from the same JSON.

## 7. Next

* Stock's `gaussian_blur` (the screen texture's and the canvas back buffer's mipmaps) has glow's layout; a benchmark
  scene that uses them (refraction, frosted glass) is needed to measure the same change there.
* The opaque pass and SSAO remain the largest passes; the tone mapping pass (0.11-0.16 ms) has no exact lever found.
