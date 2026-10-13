# FellowEngine — milestone 8 report (the opaque pass: what its time is spent on)

Status: **DRAFT** (`fellow96`-`fellow110`, measured 2026-10-12/13). Branch `fellow/geometry`. Benchmark machine as in
milestone 7 (Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop GPU, NVIDIA GPU only, Windows 11,
1920×1080, "high" preset). Decision records `docs/decisions/0037`-`0044`; user-facing summary
`docs/FELLOWENGINE_OPTIONS.md`.

## 0. Result

<!-- m8_f125_final: stock vs fellow110 defaults vs + visually lossless options -->
TBD (`m8_f125_final`).

New in this milestone and on by default, all exact: **soft shadow filter taps fetched four at a time** (§3, 0040),
**TAA velocities in shared memory** and **SSIL's last frame without its unread full-size level** (§6, 0044).

## 1. The question

After milestone 7 the opaque pass was the largest pass in every scene (Sponza 2.4-2.6 ms, Bistro exterior 1.9-2.1,
Bistro interior 1.5-1.7, Lights Lab 1.2-1.4 ms), its specialized fragment shaders at 80 registers with spills (24 of
48 warps per SM). This milestone asked where that time goes and what of it can go without changing a pixel.

Sizing, one feature removed at a time (`m8_f110_sizing`, async compute off): hard directional shadows instead of PCSS
take 0.44 ms off Bistro exterior's opaque pass; positional shadows 0.47 ms off Bistro interior's (soft positional
filtering 0.25 ms); soft directional filtering 0.23 ms off PBR Lab's. Later (`m8_f120_sizing`, 0043): the omni and spot
lights are 0.87 of Bistro interior's 1.42 ms and 0.89 of Lights Lab's 1.17 ms.

## 2. What it is not

* **Registers** (0037). Experiment bits compile each scene shader variant several ways and dump every pipeline's
  statistics: loops bounded by specialization constants are not unrolled by the driver anyway; unrolling them takes 96
  registers; the directional cascades in one loop shrink the binary by 18 % at the same 80 registers. Bistro interior
  and Lights Lab sit at 80 registers without directional soft shadows: the pressure is the general lighting code. No
  loop-level change helps.
* **The light cluster's tiles** (0038). 16- or 8-pixel tiles hand each fragment fewer lights to walk but cost more in
  the cluster build than they save (Lights Lab +2.3 % / +40 %), and are not exact (Stress Lab: 4 pixels).
* **How many filter taps run** (0039). Share maps (lavapipe) show most PCSS calls that run their taps get a uniform
  answer; deciding them from the min/max pyramid after the blocker search is exact with a 2^-15 margin (the comparison
  sampler compares at the 16-bit atlas's precision) but saves hundredths of a millisecond. Diagnostic bit 64 (PCSS
  code kept, returning lit at once) took 0.66 ms off Bistro exterior's opaque pass at unchanged registers: it is the
  filters' work, but not the number of their taps.

## 3. The taps wait for each other (0040, exact, on)

The filters' loops are not unrolled, and each iteration adds its fetch's result to a running sum before the next
fetch starts: one texture latency per tap, which 24 resident warps do not hide. Fetching four taps, then adding their
results in the same order, puts four fetches in flight with the same arithmetic. Every soft shadow loop of the
clustered renderer: directional PCF and PCSS (search, filter), spot and omni PCF, omni and spot PCSS.

* lavapipe bit for bit (five labs, captures); RTX bit for bit in Bistro exterior, at the floor elsewhere — except City
  Lab, where a handful of pixels in some frames are one level off, deterministically (`m8_f119`: on against on is bit
  for bit): the driver contracts a coordinate's multiply-add differently in the batched loop.
* In one process (`m8_f117_batch_ab`): Bistro exterior −5.0 %, PBR Lab −5.0 %, Sponza −4.6 %, Foliage Lab −3.8 %,
  Bistro interior −3.6 %, Lights Lab −2.1 %, City Lab −0.9 % (n.s.); opaque pass up to −0.33 ms.
* Batches of two wait twice as often, batches of six leave 8-tap loops to stock's loop: four is the fastest
  (`m8_f117_batch_size`). Directional and positional batching together beat either alone in every scene
  (`m8_f120_kinds_ab`), although positional batching takes the main variants to 96 registers.

## 4. The same idea elsewhere (0041, 0042, 0043: rejected)

* **SSAO/SSIL gathers** (0041): the same loop shape, the same exact batching; no gain. At 40 registers the SSAO gather
  keeps 48 warps per SM, which hide the latency already.
* **Two cascades of a blended pixel through one PCSS** (0042): exact, slower (+0.1-0.2 ms, more registers and
  local memory).
* **A blocker search shared by 2 × 2 pixel quads** (0042, visually lossless candidate): a quarter of the search's
  fetches saves at most 2 % (PBR Lab), and Foliage Lab drops to 50.8 dB with 0.9 % of pixels off by more than two
  levels: below the bar. After 0040 directional PCSS still costs 0.5 ms in Bistro exterior (diagnostic bit 64), but
  that is the texture work of two cascades' search and filter, not waiting.
* **Dropping the lights that cannot reach a pixel first** (0043): exact, slower. The light loop's cost is the lights
  that do reach the pixel.

## 5. Two exact post-processing savings (0044, on)

* **TAA v2**: the closest neighbour's velocity read from the shared tile instead of a global load after the depth
  search; the previous velocity fetched with the history. TAA pass −0.006 to −0.033 ms.
* **SSIL's last frame**: SSIL reads level 5 of a six-level copy of the frame, half-size SSR level 1; level 0 is only
  an intermediate. Level 1 made from the internal texture directly, with the copy's sanitizing: the copy region
  −0.03 ms, frame −0.5 to −1.0 % where SSIL is on.

## 6. Tooling

* `debug/scene_shader_experiments` has 16 bits: experiments and diagnostics compiled into every scene shader variant
  at start-up (unrolling, cascade loop, share maps, PCSS returning lit, batch widths, no lights, no decals/probes, the
  pair and prefilter experiments).
* `FELLOW_PIPELINE_STATS`: every pipeline's driver statistics (registers, binary size, local memory) per run;
  `.tools/scripts/pstats_cmp.py`.
* `run_benchmarks.py`: a variant's parts may come in any order (a `%` before `#` had swallowed the scene arguments in
  `m8_f107`).
* `debug/batched_taps_kinds`, `debug/cluster_size`, stat `postfx_last_frame_direct`.

## 7. Incidents

* **`m8_f107`**: one variant's spec put the start-up override before the scene arguments; the parser read the
  arguments as part of the override and the session ran the wrong layout. Parser fixed, session void.
* **`m8_f118`**: SSAO/SSIL tap batches set with `--fellow`, which the bench applies after start-up: the shaders had
  already compiled without the define, so the "batched" runs ran stock's loop. Re-run as `m8_f118b` through
  `override.cfg`; `m8_f118` serves as a null test.
* **A reserved word** (`active`) in a GLSL experiment failed to compile on lavapipe (caught before RTX).
* **`m8_f122`** (interrupted by a pause) found Bistro exterior at 94.2 dB between `fellow95` and `fellow108`; every
  pair of consecutive builds between them (`m8_f123`, 13 builds) and the same pair again (`m8_f124_recheck`) are bit
  for bit. The likely cause is the session's first run: a build whose driver pipeline cache had been evicted compiles
  pipelines during its pre-roll, and a frame drawn with the ubershader enters the TAA history.

## 8. Next

TBD.
