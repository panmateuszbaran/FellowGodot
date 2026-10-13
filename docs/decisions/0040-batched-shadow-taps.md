# 0040 — Soft shadow filter taps fetched four at a time

Status: **KEEP, on by default** (exact) · Branch `fellow/geometry` · Toggle `rendering/fellow/shadows/batched_taps`
(runtime, specialization bit 17 of `sc_packed_1`; on) · Diagnostics `debug/scene_shader_experiments` 256 (batches of 2),
512 (batches of 6), 1024 (directional PCSS loops unrolled, with the toggle off) · Code: `scene_forward_lights_inc.glsl`
(`FELLOW_TAP_BATCH`, `FELLOW_BATCH_TAPS`; `sample_directional_pcf_shadow`, `sample_pcf_shadow`, `sample_omni_pcf_shadow`,
`sample_directional_soft_shadow`, the omni and spot PCSS in `light_process_omni` / `light_process_spot`),
`scene_forward_clustered_inc.glsl` (`sc_fellow_shadow_tap_batch`), `render_forward_clustered.cpp`, `fellow_settings.*`.

## Problem

The opaque pass is the largest pass in every benchmark scene, and its soft shadow filters are its largest part
(0039: hard directional shadows instead of PCSS take 0.44 ms off Bistro exterior's 2.11 ms; hard positional shadows
0.25 ms off Bistro interior's 1.51 ms). Diagnostic bit 64 (`m8_f115`, PCSS code kept but returning lit at once,
registers unchanged at 80) took 0.66 ms off Bistro exterior's opaque pass, while skipping most of the PCF filter loops
(0039) saved hundredths of a millisecond: the cost is neither the registers nor the number of comparison taps as such.

The filters' tap loops are bounded by specialization constants (the quality's sample counts) and the driver does not
unroll them (0037: `[[dont_unroll]]` gives the same binary). Each iteration fetches one tap and adds its result to a
sum before the next iteration starts, so each fetch waits for the previous one's result: the loops run at the latency
of one texture fetch per tap, which the 24 resident warps (80 registers) do not hide.

## Change

Each tap loop takes its taps in batches of four: the batch's four coordinates and fetches first, then the four results
added in the same order with the same expressions as stock's loop (blocker search: the comparison and the two sums;
filters: the sum). The four fetches are in flight together.

```glsl
for (uint i = 0; i < n; i += FELLOW_TAP_BATCH) {
	float t[FELLOW_TAP_BATCH];
	[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
		t[j] = textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(<tap i + j>, depth, 1.0));
	}
	[[unroll]] for (uint j = 0u; j < FELLOW_TAP_BATCH; j++) {
		avg += t[j];
	}
}
```

Applied to every soft shadow loop of the clustered renderer: directional PCF and PCSS (blocker search and filter), spot
PCF, omni PCF, omni and spot PCSS (blocker search and filter). Every quality's sample counts are multiples of four
(4, 8, 12, 24, 32) except the "very low" PCF's single sample, which keeps stock's loop (a sample count that is not a
multiple of the batch takes stock's loop; the choice is a specialization constant, so each pipeline holds one of the
two). Area lights' PCSS (no benchmark scene uses area lights) and the mobile renderer are unchanged.

### Why it is exact

The same taps, the same coordinates and the same additions in the same order: only when the fetches are issued
changes. No sum is reassociated (the results are added one by one after the batch's fetches).

## Measured

### Which loops, how wide (`fellow103`, `m8_f116`; between processes, 2 runs, profiled, async compute off)

Experiment bits on the shipped shaders: 256 directional loops in batches of four, 512 positional loops, 1024 the
directional PCSS loops fully unrolled (12 taps each at the bench's quality, soft medium).

| scene | opaque pass, stock loops | directional batches (256) | positional batches (512) | directional unrolled (1024) |
|---|---:|---:|---:|---:|
| Bistro exterior | 2.103 ms | **1.817 (−0.285)** | | 1.959 (−0.144) |
| City Lab | 0.521 | **0.459 (−0.062)** | | 0.521 (−0.000) |
| Foliage Lab | 1.039 | **0.952 (−0.086)** | | 1.069 (+0.031) |
| Sponza | 2.373 | **2.169 (−0.204)** | | 2.364 (−0.009) |
| Bistro interior | 1.692 | | **1.485 (−0.207)** | |
| Lights Lab | 1.354 | | **1.260 (−0.093)** | |
| PBR Lab | 0.898 | | 0.885 (−0.013) | |

Pipeline statistics (`FELLOW_PIPELINE_STATS`): the directional batches keep the main colour-pass variants at 80
registers (binary +1.9 KB); the positional batches move Bistro interior's main variants from 80 registers with 112
bytes of local memory to 96 registers with 48 bytes; full unrolling of the directional loops gives 96 registers
everywhere, and loses most of the gain to the lower occupancy.

Batch width (`fellow104`, `m8_f117_batch_size`, both kinds of loops, 2 runs, async compute off), opaque pass against
batches of four:

| scene | batches of 4 | of 2 (bit 256) | of 6 (bit 512) |
|---|---:|---:|---:|
| Bistro exterior | 1.793 ms | +0.088 | −0.012 |
| Bistro interior | 1.466 | +0.115 | +0.172 |
| Sponza | 2.206 | +0.060 | +0.071 |
| Lights Lab | 1.255 | +0.024 | +0.079 |

Batches of two still wait twice as often (and reach 96 registers in Bistro interior); batches of six leave the
positional PCF's 8 taps (12 % 6 = 0 but 8 % 6 ≠ 0) to stock's loop. Four is kept.

### Images

* lavapipe, toggle off against on (captures, 960 × 540): City Lab, Foliage Lab, Lights Lab, PBR Lab, Blend Lab —
  **0 differing pixels**; the same for the experiment bits (directional and positional batches of four, City Lab,
  Foliage Lab, Blend Lab, Lights Lab, PBR Lab; batches of two in City Lab, of six in Lights Lab; the unrolled loops in
  City Lab and Foliage Lab). The mobile renderer compiles and renders City Lab (its shaders keep stock's loops).
* RTX, off against on (`m8_f117_batch_temporal`, 60-frame sequences, 1080p, TAA):

| sequence | PSNR mean / min | max \|d\| | pixels > 2 | the floor (exact changes measured before) |
|---|---:|---:|---:|---|
| `bistro_ext_path` | 99.0 / 99.0 dB | 0 | 0 | bit for bit |
| `city_pan` | 101.2 / 99.0 | 1 | 0 | 0039's exact toggle: 101.2, max \|d\| 1 (`m8_f114`) |
| `lights_walk` | 91.9 / 79.4 | 2 | 0 | 90.9-94.1 (0038) |
| `foliage_walk` | 78.2 / 74.8 | 12 | 0.000 % | 78.3 / 74.9 (`m7_f101`) |
| `specular_orbit` (PBR Lab) | 76.3 / 72.8 | 12 | 0.000 % | 73.6 (0038) |
| `sponza_pan` | 75.5 / 72.6 | 2 | 0 | 75.5 (0039) |
| `bistro_int_path` | 67.1 / 65.9 | 11 | 0.001 % | 67.1 (0038) |

Bistro exterior bit for bit; Lights Lab, Foliage Lab, PBR Lab, Sponza and Bistro interior at their run-to-run
floors (a null test of `fellow105`, `m8_f118_ss_temporal`, two runs of the same shaders: Bistro exterior and City Lab
bit for bit, Bistro interior 67.1, Lights Lab 94.3, PBR Lab 76.4, Sponza 75.5 dB). **City Lab is not bit for bit**:
in some frames a few pixels differ by one level (mean 101.2 dB, min 99.0 = identical frames; no pixel off by more
than 1), where its null test is. The arithmetic is the same expressions in the same order, and lavapipe renders it bit
for bit; NVIDIA's compiler may contract a multiply-add of a tap coordinate differently in the batched loop (SPIR-V
allows contraction), which moves a tap across a texel boundary in rare pixels. The same signature as 0039's runtime
toggle in City Lab (101.2 dB, max |d| 1).
It is deterministic (`m8_f119_city_temporal`, `fellow106`): off against on 101.18 dB, on against a second run of on
bit for bit, off against that second run 101.18 dB again.

### Performance (`fellow104`, `m8_f117_batch_ab`: one process, defaults with autotune off, blocks of 30 frames, off
and on alternating, 800 block pairs per scene)

| scene | frame | GPU time | opaque pass |
|---|---:|---:|---:|
| Bistro exterior | **−5.0 %** (5.50 → 5.23 ms) | −5.1 % | 2.137 → 1.811 ms (−0.326) |
| PBR Lab | **−5.0 %** (2.71 → 2.58) | | 0.861 → 0.751 (−0.110) |
| Sponza | **−4.6 %** (7.51 → 7.16) | | 2.338 → 2.122 (−0.216) |
| Foliage Lab | **−3.8 %** (3.56 → 3.43) | | 1.088 → 0.966 (−0.122) |
| Bistro interior | **−3.6 %** (4.25 → 4.09) | | 1.459 → 1.366 (−0.094) |
| Lights Lab | **−2.1 %** (2.86 → 2.80) | | 1.217 → 1.175 (−0.042) |
| City Lab | −0.9 % (p 0.31) | | 0.535 → 0.481 (−0.054) |

All p < 10^-20 except City Lab (its frame is bound elsewhere; its opaque pass is 10 % shorter).

## Decision

**KEEP, on by default** (`fellow104`): the same arithmetic as stock (lavapipe bit for bit; RTX bit for bit or at the
floor, except a handful of pixels one level off in some City Lab frames from the driver's rounding), frame −2 to −5 %
in every scene with soft shadows. The lesson for the rest of the renderer: loops that are not unrolled and feed each fetch's result
into a running sum pay one fetch latency per iteration; SSAO and SSIL gathers have the same shape (decision 0041).
