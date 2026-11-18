# 0271 — A terrain hole's fragments discarded at the end: their quads' derivatives defined

Status: **KEEP** (a fix) · Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_terrain_3d.cpp`
(`_terrain_shader_code()`: the holes' material) · Checks: the terrain-holes scene's frames 60, 90 and 120 in two runs on
the laptop's Intel GPU (`th_frames.py`, `m146_f503`); the image regression suite (`terrain_holes` blessed again).

## Problem

Milestone 136 found the regression suite's terrain with holes differing by up to 5 levels from run to run on the
laptop's Intel GPU, only with FellowEngine's renderer on; milestone 143 put it in the patches over the holes, and none
of 23 of FellowEngine's settings or groups off removed it; milestone 145 found it from frame to frame within one run
too (a still view: frames 60, 90 and 120 up to 4 levels apart), and the frames of FellowEngine off the same bit for
bit.

## Cause

The patches over holes are drawn with a second material (decision 0172) whose fragment shader began by discarding the
fragments of a triangle touching a hole — and then sampled the layers' textures with implicit derivatives and took
`dFdx()` / `dFdy()` of the position (the triplanar layers, the virtual texture's mip). A derivative is taken across a
2 × 2 quad of fragments; once one of them has been discarded the quad's derivatives are undefined, and on the holes'
rims every quad straddling the edge had one. On lavapipe what came out was the same every frame; on Intel's GPU it
changed from frame to frame (presumably whatever the discarded lanes' registers held), and differently with
FellowEngine's scene shader than with stock's (another compiled shader).

## Change

The hole test is kept at the top (a `bool`), the discard moved to the end of the material's `fragment()`, after every
texture fetch and derivative: the rim's quads are whole through them. The hole's fragments do the layers' work and are
then dropped (only the patches over holes have this material).

## Result

| Terrain holes, Intel GPU, still view (two runs, frames 60/90/120 against run 0's frame 60) | largest difference | identical |
|---|---:|---:|
| milestone 142's build (discard first) | 3-5 levels | 0 of 5 |
| the same, discard as demote to helper (`debug/scene_shader_experiments` 4194304) | 1 level | 0 of 5 (1 of 1 within run 1) |
| **discard at the end** (`m146b`) | **0** | **5 of 5** |

On lavapipe the holes' rims change by up to 4 levels (0.08 % of `terrain_holes`' picture: the rim's quads now with
derivatives of their whole quad); the golden blessed again; every other scene of the suite exact.

## Decision

KEEP: the terrain's holes are the same in every frame on every GPU we have. Godot's other materials that discard before
sampling have the same undefined derivatives on such GPUs (stock's semantics of `discard`); a scene that relies on them
can move its discard to the end the same way, or test with `debug/scene_shader_experiments` 4194304 (demote).

## Rejected / not done

* **Demote to helper for every discard** (SPIR-V's `OpDemoteToHelperInvocation`, the experiment bit 4194304): defined
  derivatives for every material, but a change to stock's semantics of every discarding material (and one level still
  differed); the terrain's own shader is enough here.
