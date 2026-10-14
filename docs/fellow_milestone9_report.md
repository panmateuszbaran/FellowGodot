# FellowEngine — milestone 9 report (the positional lights)

Status: **FINAL** (`fellow111`-`fellow127`, measured 2026-10-12/14). Branch `fellow/geometry`. Benchmark machine as
before (Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop GPU, NVIDIA GPU only, Windows 11, 1920×1080,
"high" preset). Decision records `docs/decisions/0045`-`0050`; user-facing summary `docs/FELLOWENGINE_OPTIONS.md`.

## 0. Result

New in this milestone and on by default, all exact: **the omni and spot lights' fields read together, before their
tests** (0045), **light tile culling** under autotune (0049), and a fix for a crash of stock's pipeline cache at 2 GiB
(0050). Three other ideas were measured and rejected (0046, 0047, two experiments of 0048).

Frame time, `m9_f145_final` (3 interleaved runs per build and scene, 20 s each after a 30 s heat soak; paired
statistics): stock Godot 4.7.2, milestone 8's final build (`fellow110`), this milestone's (`fellow127`) with its
defaults, and with the visually lossless options (`postfx/ssao_temporal`, `postfx/ssil_temporal`,
`postfx/fog_half_rate`, `shading/adaptive_vrs`).

| scene | stock ms | m8 defaults | **m9 defaults** | m9 vs stock | m9 vs m8 (pairs faster) | m9 lossless | vs stock |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sponza | 13.591 | 6.907 | 7.123 | **−47.6 %** | +3.1 % (0/3, n.s.; GPU clocks 1 579-1 748 MHz) | 6.897 | −49.3 % |
| Bistro exterior | 6.771 | 4.490 | 4.551 | **−32.8 %** | +1.4 % (1/3, n.s.) | 4.371 | −35.4 % |
| City Lab | 2.932 | 2.081 | 2.119 | **−27.7 %** | +1.8 % (0/3, n.s.; render thread +2-3 %) | 2.070 | −29.4 % |
| Lights Lab | 3.026 | 2.686 | 2.472 | **−18.3 %** | **−8.0 %** (3/3, p 0.016) | 1.956 | −35.4 % |
| Bistro interior | 4.199 | 3.602 | 3.536 | **−15.8 %** | −1.8 % (3/3, p 0.04) | 3.258 | −22.4 % |
| Foliage Lab | 3.602 | 3.155 | 3.128 | **−13.2 %** | −0.9 % (2/3, n.s.) | 3.120 | −13.4 % |
| PBR Lab | 2.558 | 2.306 | 2.367 | **−7.5 %** | **+2.6 %** (0/3, p 0.016) | 2.183 | −14.6 % |
| Stress Lab | 4.940 | 4.720 | 4.831 | −2.2 % (n.s.) | +2.4 % (1/3, n.s.) | 4.669 | −5.5 % |

Foliage Lab with its vegetation in one MultiMesh per type (`m9_f145_foliage_single`): stock 10.756 ms, `fellow127`
5.066 ms (**−52.9 %**, 3/3 pairs).

**A regression against milestone 8 in PBR Lab**: its GPU time went from 2.236-2.262 ms (`fellow110`) to 2.285-2.327
(`fellow127`) at the same clocks (1 764-1 808 MHz), in all three pairs. City Lab and Stress Lab are 1.8-2.4 % slower too
(not significant, but City Lab in none of the pairs faster, its render thread 2-3 % slower). The in-process A/Bs of this
milestone's changes did not show it (0045 v2: PBR Lab −0.4 %); `m10_f148` compares the two builds pass by pass and the
defaults against each change switched off. Until it is explained, this milestone's gains are Lights Lab and Bistro
interior, and the defaults cost up to 2.6 % elsewhere.

**Follow-up** (milestone 10, `m10_f148`, `m10_f150`): in one process, `fellow129` with `lights/fields_early` off is as
fast as with it on in PBR Lab (+0.0 %), and with `lights/tile_cull` off 0.5 % faster (its autotune evaluations); City
Lab and Stress Lab show neither. Between processes, nine builds from `fellow110` to `fellow130` in PBR Lab (3 runs each)
differ by −1.4 to +1.8 % from `fellow110` at the same clocks, without a step at any change (`fellow123` −1.4 %,
`fellow127` +1.3 %, nothing between them but autotune's tunable and the pipeline cache limit): build-to-build variation
of about ±2 %, which `m9_f145`'s +2.6 % does not clearly exceed. Milestone 10's `fellow130` is 1.7 % faster than
`fellow110` there (3/3 pairs).

Images, `fellow127`'s defaults against `fellow110`'s (`m9_f145_defaults_temporal`, 60-frame sequences): Bistro
exterior, City Lab and Stress Lab bit for bit; Lights Lab 100.0 dB mean (max |d| 2); Bistro interior 67.1, Foliage Lab
78.3, PBR Lab 76.4, Sponza 75.5 dB, their run-to-run floors.

## 1. The question

Milestone 8 left the omni and spot lights as the largest part of the opaque pass where there are many of them: 0.87 of
Bistro interior's 1.42 ms and 0.89 of Lights Lab's 1.17 ms (0043). This milestone asked where that time goes and took
it apart: the light loop, the reads of the lights' data, the shadow lookups, the shading.

## 2. Where the lights' time goes (0045)

Diagnostics remove one part at a time with the code kept (`debug/scene_shader_experiments` 65536: no `light_compute()`;
131072: no shadow lookups; 524288: lights stop after their range test; 262144 with 8 or 32: light counts as the
colour):

* Bistro interior: the positional shadow lookups are a third of the opaque pass (0.46 ms); shading almost nothing.
* Lights Lab: the loop and the range tests 0.63 ms of the lights' 0.92 ms; shading 0.12, shadows 0.15.
* Per pixel (lavapipe, Lights Lab): the loop visits 10-16 lights, 3-8 are within range, 2.6-7.6 are shaded.

## 3. The light's data read together (0045, exact, on)

A light's fields were read where each is used, each read waiting behind the tests before it. v1 read the hot fields at
the start of the light's function; v2 (`fellow117`) reads them in the loop with the layer mask and the bake mode, right
after the light's index. Bistro interior −3.1 %, Lights Lab −3.0 % (in one process), City Lab bit for bit, the other
sequences at their floors. Used in frames with omni or spot lights in view (the specialization bit otherwise only
changes code: Sponza +0.5 % in v1).

## 4. What did not work: more registers (0046, 0047, 0048)

* **The next light read while the current one is processed** (0046, four modes, with the next cluster word): exact,
  2-11 % slower.
* **The loop's fields from an 80-byte copy** in a storage or a uniform buffer (0047): exact, 1.4-2.4 % slower (storage),
  neutral (uniform).
* **Positional PCF taps in one batch of 8**; **shadow fields read after the range test** (0048): slower; no change.

The finding (0048): the colour pass's main variants sit at 96 registers with 48-64 bytes of spills. Any change that
keeps a few more values live makes the driver stop spilling and allocate 114-128 registers: 16-17 warps per SM instead
of 20, in a pass that waits on memory. The colour pass has a register budget; changes have to fit in it.

## 5. Light tile culling (0049, exact, on under autotune)

After the depth pre-pass a compute pass takes each 8 × 8 pixel tile: the nearest and farthest depth of its pixels, the
view-space box of its frustum between them (widened by a margin), and for every omni and spot light of its cluster tile
whether the light's sphere meets the box. The opaque colour pass ANDs each cluster word with the tile's: the light
loops visit the same lights in the same order, minus those out of range of every fragment of the tile (which the range
test would skip: with the depth pre-pass, the colour pass shades only at the stored depths). Only where that holds
(materials whose out-of-range lights are skipped, no `LIGHT_VERTEX`, no depth writes, one view, no MSAA, no VRS).

* lavapipe: the light loop visits half the lights (Lights Lab: 9.8-16.1 → 4.6-9.2 per pixel); images identical.
* The pass: 0.15-0.23 ms with a workgroup per tile (v1), 0.034-0.054 ms with one per 16 × 16 pixels (v2-v5).
* In one process (v5): Lights Lab **−5.8 %** (opaque pass −0.21 ms), but Bistro interior +4.9 %, PBR Lab +2.7-3.9 %:
  where the culling saves little, using the masks slows every pass of the frame by a few per cent, which diagnostics
  (`debug/tile_cull_passthrough`, `debug/tile_cull_pass_only`) separate from the code path but do not explain.
* So it is an autotune tunable, starting off and switched on per scene only when measured faster: between processes
  (`m9_f143`/`m9_f144`) Lights Lab −5.6 %, the other scenes left off after the first evaluations.

## 6. A crash of stock's pipeline cache (0050)

Every run of `m9_f141` crashed (in `hash_murmur3_buffer`, saving the Vulkan pipeline cache): the bench's cache file had
grown to 2 146 666 778 bytes. Stock never evicts from the cache and checksums it with an `int` length; at 2 GiB the save
reads past its buffer. A file over `frame/pipeline_cache_max_mb` (1024 MiB) is now ignored at start-up and replaced,
and a cache that would overflow the checksum is not saved.

## 7. Tooling

* Diagnostics bits 65536-524288 (lights), 1048576 and 2097152 (experiments of 0048); `debug/scene_shader_experiments`
  now has 22 bits.
* `debug/tile_cull_passthrough`, `debug/tile_cull_pass_only`; stat `lights_tile_cull_frames`.
* A/B sessions with up to five configurations in one process (`m9_f135_pf_ab`, `m9_f140_tc_ab`).

## 8. Incidents

* **The laptop switched off overnight** (13-14 October): the queue runner stopped; the next job waited until it was
  started again.
* **`m9_f141`**: every run crashed (0050); the session is void, `m9_f143` repeats it.
* A shell command of mine matched its own pattern (`pkill -f`) and ended itself; nothing else was affected (lavapipe
  test restarted).

## 9. Next

* **The regression against milestone 8** in PBR Lab (§0): `m10_f148`.
* **The light cluster build** (0051, `fellow128`, being measured in `m10_f146`): the per-element draws of the proxies
  (instanced), the pack pass's walk of each tile's elements (one invocation per element), and a 21.5 MB clear every
  frame (left out: the pack pass leaves the buffer cleared). 0.22 ms of Lights Lab's frame plus the clear. Exact
  (`debug/cluster_verify`: lavapipe 0 mismatches in four scenes, its negative control 31 234).
* **Glow's blur** (0052, `fellow129`, `m10_f147`): its vertical pass reads shared memory with an 8-way bank conflict on
  every access; a padded layout, and 16 × 16 tiles (2.25 samples per output instead of 4). The same sums; lavapipe
  identical.
* **Bistro interior's positional shadows** (0.46 ms of its opaque pass): the lookups' texture work, within the register
  budget of 0048.
* **The cost of light tile culling's masks in Bistro interior**: every pass slower by a few per cent with them in use.
* **The post-processing chain** (SSAO 0.4-0.5 ms with its blur and interleave, TAA 0.22-0.25, glow 0.16-0.19, tonemap
  0.13-0.16 ms): about 1 ms per frame in every scene; glow and tonemap have not been looked at yet.
