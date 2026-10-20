# 0072 — Movie Maker mode: a 3D draw waits for its specialized pipeline

Status: **KEEP** (on from `fellow161`) · Branch `fellow/geometry` · Setting `rendering/fellow/frame/movie_wait_pipelines`
(on; acts only while Movie Maker mode writes) · Code: `render_forward_clustered.cpp` (`_render_list_template`),
`fellow_settings.*`.

## Problem

Since Godot 4.4 a 3D draw whose specialized pipeline is not compiled yet is drawn with the ubershader, and the
specialized pipeline is compiled in the background. The frame does not wait, at the price of drawing some frames with
a different (slower, and in the last bits different) shader. In a game that is the right trade. In Movie Maker mode
(`--write-movie`), which records a video at a fixed frame rate and does not run in real time, it is not: the frames
drawn while pipelines compile depend on the compile threads' timing, so two recordings of the same scene differ, and
the first appearance of every material is recorded with the ubershader. Godot's 2D renderer already waits for its
pipelines.

## Change

`frame/movie_wait_pipelines`: while Movie Maker mode writes a video, the 3D renderer asks for the specialized pipeline
with `wait_for_compilation` (as it does for the ubershader) instead of falling back. Outside Movie Maker mode nothing
changes (the setting is not read for games or for the benchmark, which does not write a movie).

## Verification

lavapipe, PBR Lab, two cameras with 6 frames each (pipelines still compiling), `--write-movie` (PNG sequence, every
frame), autotune off:

| | draws on the ubershader | pipeline waits | two recordings against each other |
|---|---|---|---|
| setting on | **0** | 17 | **bit for bit** (14 of 14 frames) |
| setting off (stock's behaviour) | 34 | 2 | — |
| on against off | | | 13 of 14 frames differ, max \|d\| 34 |

RTX (`m14_f211`, `fellow161`, Movie Maker mode at 1920 × 1080 from the scene's first frame, 62 frames each): Lights
Lab's street camera and Sponza's upper floor, two recordings with the setting **bit for bit**, and the same as the
recording without it: with warm shader and pipeline caches, Godot's load-time pipeline compilation evidently had the
pipelines ready before they were drawn (the first attempt, `m14_f209`, wrote no frame with a relative `--write-movie`
path; with an absolute one it does). The setting matters where a pipeline is still compiling when a
material is first drawn: cold caches, or materials that come into view later.

## Decision

**KEEP, on.** Recordings are drawn with the scene's own pipelines from the first frame and are the same on every run;
the cost is the compile time, which Movie Maker mode does not show (it does not run in real time).
