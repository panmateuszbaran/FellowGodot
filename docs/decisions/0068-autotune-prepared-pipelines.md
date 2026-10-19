# 0068 — Autotune: an evaluation that switches a pipeline bit starts once its pipelines are compiled

Status: **MEASURING** · Branch `fellow/geometry` · Setting `rendering/fellow/autotune/prepare_pipelines` (on) · Code:
`fellow_autotune.*` (`preparing`, `autotune_prepare_flips()`, `autotune_prepare_report()`),
`render_forward_clustered.*` (`_fellow_prepare_flipped()`, `_fellow_prepare_request()`,
`RenderListParameters::fellow_prepare_flips`), `fellow_settings.*` (stats `autotune_prepare_frames`,
`autotune_prepare_requests`, `autotune_prepare_timeouts`).

## Problem

Four of autotune's tunables are specialization constants of pipelines: the directional and the positional shadow
pyramids' early-outs (0005) and light tile culling (0049) in the colour passes, persistent instance data (0022: the
lists' format) in every pass. An evaluation switches one of them in blocks of frames; the first frame of a block that
uses the other value with a material, a vertex format and the frame's other bits compiles that pipeline, and its draws
use the ubershader until it is ready. Stress Lab compiles 40-88 specialization pipelines during every final's 20 s
measurement after a warm-up that compiled 70 (stock none), with 129-3964 draws on the ubershader; warm sequences
compile 2-50 during their 60 captured frames with autotune on, none with it off (milestone 13, section 4). 0064 leaves
such frames out of the evaluation's measurement; they are still slower frames and, on lavapipe, other images (0057: an
alpha-scissor material's ubershader and its specialized pipeline differ where derivatives after `discard` are
undefined).

Compiling the other value of every pipeline ahead whenever one compiles (0066) was removed: up to four pipelines for
one wherever autotune never switches the bits, at low priority a hang at exit on lavapipe, at normal priority
competition with the compilations the frame needs.

## Change

`autotune/prepare_pipelines`: when the evaluation due is one of those four tunables, it is prepared first. In the
frames of the preparation the passes that use the bit (the opaque pass for light tile culling, where it can run; the
opaque, motion and transparent passes for the pyramids; every pass for persistent instance data) note, for each
pipeline they bind, the one with the tunable's bit the other way, if the pipeline map does not hold it (a read of the
map, safe while lists are recorded in parallel); after the passes the render thread requests the missing ones at high
priority and reports how many are still compiling. The evaluation starts at the first frame after one that found them
all compiled, or after 240 frames. Outside preparations nothing changes; the pipelines requested are those the
evaluation's blocks would compile on their first frames, for the materials in view.

Nothing changes in what is drawn: preparation frames draw as the frames before them.

## Verification

lavapipe, 480 × 270, perf mode (100-frame passes of the flythrough, 60 s), autotune on with a shortened schedule so
that a run holds several evaluations (`autotune/interval_frames` 100, `block_frames` 8, `max_duty_percent` 50), the
setting on against off; the counters of the measured part:

| scene | evaluations (prepared) | draws with the ubershader | evaluation frames not counted (0064) | pipelines requested ahead |
|---|---|---|---|---|
| PBR Lab | 2 (2) / 2 | **13** / 73 | 2 / 8 | 26 |
| Lights Lab | 3 (3) / 3 | **1** / 10 | 0 / 0 | 6 |
| City Lab | 3 (3) / 2 | **1** / 4 | 0 / 0 | 7 |

Each preparation took 2-9 frames (lavapipe compiles slowly; at most 6 pipelines missing per frame); no timeout. The
compilations during the measurement are the same in number (PBR Lab 11 / 11): they happen before the evaluation's
blocks instead of in their first frames. What remains drawn with the ubershader are materials that come into view
during an evaluation with the bit the other way (the camera moves; a preparation covers what is in view).

RTX: (to measure — Stress Lab, autotune on, the setting on against off: compilations and ubershader draws during the
measurement, frame time)

## Decision

(pending the RTX measurement)
