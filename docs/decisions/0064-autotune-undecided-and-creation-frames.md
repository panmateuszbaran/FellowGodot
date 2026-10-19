# 0064 — Autotune: an undecided evaluation continued next; frames that create pipelines not counted

Status: **KEEP**, both on (`fellow153`) · Branch `fellow/geometry` · Settings `rendering/fellow/autotune/continue_undecided`,
`rendering/fellow/autotune/skip_creation_frames` (both on) · Code: `fellow_autotune.cpp` (`TuneState::continue_next`,
`creation_count()`, `autotune_frame_end()`), `fellow_settings.*` (stat `autotune_tainted_frames`).

## Problem

`m12_f178_final` measured Lights Lab 3.5 % slower with `fellow151` than with milestone 11's `fellow142` (GPU
2.287 → 2.367 ms, p 0.025). Light tile culling (0049, chosen by autotune) ran 3442-3705 of the 8813 measured frames
against 8266-8746.

## Evidence

* `m12_f183_lights_bisect` (three runs each of `fellow142`, `fellow147`, `fellow150`, `fellow151`, `fellow151` without
  0062's barriers, verbose log): the tile culling frames of each run are 8266 (or 8737) or 3442, in every build —
  `fellow142` 8266 / 3442 / 3442, `fellow147` 8266 / 8266 / 8737, `fellow151` 4242 / 3442 / 8266. Frame time between
  the builds within noise (−3.4 to +0.4 %, p ≥ 0.12). Not a change of the code: a coin toss of autotune's.
* `m12_f183_tc_ab` (in one process, autotune off, tile culling off / on): Lights Lab GPU −5.5 % (`fellow142`) and
  −5.8 % (`fellow151`), 0.134 / 0.137 ms; the culling is worth the same in both.
* The verbose logs: light tile culling starts off and is switched on only by an evaluation that measures it faster
  (0049). Its first evaluation (from frame 4261) measures it 0.087-0.108 ms faster with a standard error of
  0.030-0.039 ms: t = 2.2-3.6 against the 2.5 required. Undecided, it is evaluated again at the base interval, but four
  other tunables' evaluations were due first, each followed by the duty limit's pause (four times its length): the
  next one came at frame 9286, 5000 frames (12 s) later, and decided on early (0.14 ms, se 0.03).
* One of `fellow151`'s first evaluations measured 0.446 ms with a standard error of 0.307 ms: a pair far from the
  others (the same run had just switched persistent instance data off). AUTOTUNE_SKIP drops each block's first four
  frames; a specialization compiling for longer than that (the opaque pass's pipelines with the tile culling bit, used
  for the first time in the evaluation's first "on" block) is drawn with the ubershader in the counted frames.

## Change

1. `autotune/continue_undecided`: a tunable that starts off (light tile culling, adaptive shading), left off by an
   undecided evaluation whose pooled pairs favour it, is evaluated again next — as soon as the duty limit allows,
   before the other tunables' scheduled evaluations — until decided or until its pool holds 36 pairs.
2. `autotune/skip_creation_frames`: a frame of an evaluation in which a draw used the ubershader (or 0059's fallback)
   because its pipeline was still compiling, or in which the render thread created a pipeline or a shader or waited
   for one, is not counted (neither its GPU time nor its wall time). Stat `autotune_tainted_frames`.

Neither changes an image: autotune switches exact tunables (and adaptive shading, visually lossless) as before.

## Verification

lavapipe, Lights Lab with the verbose log (480 × 270, autotune on): evaluations as before; light tile culling's first
evaluation undecided and leaning on, scheduled next; one frame of an evaluation not counted (a draw with the ubershader).

RTX, Lights Lab, autotune on, eight runs of each between processes (`m12_f184_lights`, verbose log):

| build | tile culling frames of 8813 (mean / lowest) | frame time | GPU |
|---|---:|---:|---:|
| `fellow151` | 7044 / 3435 | 2.428 ms | 2.358 ms |
| `fellow153` (both on) | **7780 / 6945** | 2.435 (+0.3 %, p 0.91) | 2.363 |
| `fellow153`, `continue_undecided` off | 6623 / 192 | 2.421 (−0.3 %, p 0.91) | 2.353 |
| `fellow153`, `skip_creation_frames` off | 7807 / 6465 | 2.402 (−1.1 %, p 0.61) | 2.332 |

With the continuation no run is left without tile culling for long: an undecided first evaluation (frame 4261-4462,
0.058-0.075 ms faster, se 0.033-0.049 ms) is followed at frame 5266 by one that ends early with the culling on (0.16-
0.22 ms, se 0.016-0.054 ms), where before the next came at frame 9286-9811 (runs with 3435-3442 frames of culling) or
not at all in the run (192). The frame time between processes does not show it: this session's runs differ by up to
15 % (each build's first run 2.54-2.70 ms, its others 2.30-2.51 ms), more than the 0.137 ms tile culling saves (−5.7 %,
`m12_f183_tc_ab`) on the 1000-5000 frames in question. The frames not counted are rare (0-41 per run).

The visually lossless options (adaptive shading starts off too; `m12_f184_lossless`, four runs each): Lights Lab
1.889 → 1.893 ms (+0.3 %, p 0.79), PBR Lab 2.024 → 2.027 ms (+0.1 %, p 0.90); adaptive shading's coarse tiles and
autotune's evaluations as before. Bistro interior, where tile culling costs (`m12_f184_bint`, four runs each):
3.327 → 3.387 ms (+1.8 %, p 0.18, within this scene's run-to-run spread); its evaluations of tile culling measure it
slower (off − on −0.05 to −0.14 ms), are never continued, and leave it off as before (96-192 frames of culling, the
evaluations' own).

## Decision

**KEEP, both on** (`fellow153`). The continuation removes the runs in which a tunable worth having stays off for most
of the run (Lights Lab's lowest tile culling count 3435 → 6945 frames of 8813) without changing anything where the
first evaluation decides or where the tunable costs; frames that create pipelines are rare, and leaving them out is
the measurement autotune means to make. Neither shows in the frame time between processes at this machine's spread.
