# 0026 — Occlusion culling against a depth pyramid

Status: **MEASURING** (step 1 implemented, `fellow76`) · Branch `fellow/geometry` · Toggle
`rendering/fellow/geometry/occlusion_culling` (runtime, off) · Debug `rendering/fellow/debug/occlusion_stats`,
`debug/occlusion_cull_approx` · Code: `shaders/effects/fellow_hzb.glsl`, `fellow_occlusion_test.glsl`,
`fellow_occlusion_cull.glsl`, `render_forward_clustered.{h,cpp}` (`_fellow_occlusion_*`), scene shader binding 41,
`fellow_settings.*`.

## Problem

After milestone 5 most of the GPU frame is the passes that draw geometry (Sponza: depth pre-pass 0.98 ms, opaque 2.08,
transparent 1.27; Bistro exterior: 0.53 and 1.81 ms) and SSAO. Every element of the camera's opaque list is drawn twice
(depth pre-pass, opaque pass) whether or not anything of it is visible; only the frustum culls. Hidden geometry costs
its vertex work in both passes, and hidden alpha-tested geometry also its fragment work in the pre-pass (a shader that
discards cannot be culled by early depth testing).

## Step 1: how much is hidden

With the debug setting, after the depth pre-pass a depth pyramid is built from the depth buffer (each texel the farthest
depth, reverse Z the smallest value, of the 2 × 2 below; level 0 at half the screen, power-of-two sides) and every
opaque list element's world AABB (its instance's) is projected and tested: hidden if its nearest corner is farther than
the farthest depth of the at most 3 × 3 pyramid texels covering its projection (an element with a corner behind the
camera counts as visible). Counted: elements and the indices their surfaces draw at full detail. Nothing is culled.
This frame's own depth is the ideal case; a real culling pass has to work from the previous frame's visibility.

lavapipe, City Lab captures (aerial, street, roofs; 960 × 540, settling frames included): **64 % of the opaque elements
and 70 % of their indices hidden.**

RTX (`fellow74`, `m6_f76_occlusion_stats*`, the perf camera paths, every frame tested):

| scene | elements hidden | indices hidden |
|---|---:|---:|
| City Lab | 70.1 % | 78.8 % |
| draw-heavy City Lab | 70.1 % | 78.8 % |
| Bistro exterior | 70.3 % | 44.8 % |
| Bistro interior | 28.1 % | 18.5 % |
| Sponza | 13.2 % | 1.4 % |
| Lights Lab | 6.2 % | 6.2 % |
| PBR Lab | 0.2 % | 0.4 % |

City Lab and Bistro exterior hide most of their elements (streets between buildings); Sponza's large meshes are
almost all partly visible.

## Step 2: what culling would save (debug, wrong images)

`debug/occlusion_cull_approx`: the test also writes each surface slot's visibility, read back asynchronously; in the
following frames the camera's opaque and motion lists leave out the elements last found hidden (multimesh and particle
elements are kept). The images are wrong (an element that becomes visible appears a few frames late), but the GPU and
CPU time with the hidden elements left out, net of the pyramid and the test, sizes what an exact culling could gain.

RTX (`fellow75`, `m6_f77_occ_ab*`, in one process, blocks of 30 frames; o0: off, o1: the hidden elements left out):

| scene | frame | GPU time | render thread | depth pre-pass GPU | opaque pass GPU |
|---|---:|---:|---:|---:|---:|
| Bistro exterior | **−4.3 %** | **−4.7 %** (−0.215 ms) | **−20.4 %** (−0.44 ms) | −0.066 ms | −0.145 ms |
| draw-heavy City Lab | +2.3 % | **−4.0 %** (−0.076 ms) | +4.4 % | +0.006 ms | −0.063 ms |
| City Lab | −1.2 % | −1.5 % (−0.031 ms) | +10.1 % | +0.015 ms | −0.045 ms |
| Bistro interior | +1.1 % | +1.2 % | −1.4 % | +0.051 ms | −0.012 ms |
| Sponza | +0.4 % | +0.5 % | +1.6 % | +0.048 ms | −0.012 ms |

What it costs here: the pyramid and the test (~0.05 ms of GPU time inside the pre-pass region: Bistro interior and
Sponza, which hide little) and, on the render thread, building and uploading every element's AABB (+0.09 to +0.11 ms
in City Lab). What it saves: the opaque pass's work for hidden elements (all scenes that hide much), their pre-pass
work (Bistro exterior) and, in this debug mode only, their recording on the render thread.

## Plan

An exact version cannot leave elements out on the CPU (an element that becomes visible must be drawn in the same
frame), so it keeps recording every draw and lets the GPU set the instance counts:

1. **The opaque pass** (exact): after the full pre-pass, the elements hidden by its depth pyramid can have no fragment
   that passes the opaque pass's depth test (their nearest point is strictly behind the depth buffer everywhere they
   cover), so leaving them out changes nothing. Per run of instanced elements, the visible ones are compacted (in
   order) into the elements the shaders read (persistent instance data's 16-byte elements) and the draw becomes indirect
   with that count. Bistro exterior's −0.145 ms, City Lab's −0.045 ms, draw-heavy's −0.063 ms of opaque pass, less the
   pyramid and the test.
2. A cheaper pyramid (one dispatch for several levels, level 0 not padded to a power of two), and the AABBs kept on the
   GPU per surface slot, rewritten only when their instance changes (as persistent instance data's entries).
3. Later, the pre-pass in two phases (previous frame's visible elements, pyramid, the newly visible ones): its draw
   order changes, so coplanar ties can resolve differently; to be measured as such.

## Step 1 implemented (`fellow76`, `17e4d43`)

Before the opaque colour pass (after the pre-opaque effects, so the depth buffer is the one the pass tests against):

* The CPU walks the opaque list's runs as the colour pass will and marks the cullable ones: no multimesh, particles,
  skinning or blend shapes; a material that does not move vertices (VERTEX, POSITION, matrices, Z clip scale, trails,
  world coordinates), write DEPTH, read INSTANCE_ID, use stencil or a depth test other than the standard one; an index
  array. Only with persistent instance data (the shaders read the 16-byte elements), a single view, no MSAA, no debug
  draw mode.
* Per surface slot, the surface's local bounding box is kept in a GPU buffer (written when a slot is given to a surface,
  uploaded with the next culled frame). The GPU tests each element of a marked run: the box's 8 corners under the
  instance's transform (read from its persistent instance data entry, the one the vertex shader uses) and the camera's
  view-projection (with TAA jitter), against the depth pyramid. Margins: depth 2^-10 relative + 2^-20 absolute (the
  difference between this transform and the vertex shader's, 24-bit depth), one pixel on every side of the projection.
* Visible elements are copied in order to the same positions of a compacted element buffer (scene shader binding 41;
  an element index with its top bit set reads there), the run's indirect draw gets their count. Runs of up to 16
  elements: a thread per run; longer runs: a workgroup per run (prefix sums in shared memory).
* The colour pass draws marked runs with `draw_list_draw_indirect` (clustered surfaces, point size emulation and
  multimesh indirect keep their own paths).

Exactness: an element left out has every corner's depth (reverse Z) below the farthest depth of the pyramid texels
covering its pixels (with the margins), so each of its fragments fails the colour pass's depth test (equal after the
pre-pass, or greater-or-equal; the buffer only gets nearer); the visible ones are drawn in the same order with the same
instance data. Same images expected bit for bit.

## Step 1 measured (`fellow76`, `m6_f79_*`)

Counters (RTX, perf paths, culling on): City Lab 68 % of the tested elements hidden (77 % of their indices), Bistro
exterior 71 % (34 %), Bistro interior 33 % (16 %), Sponza 31 % (2 %: its large meshes are drawn by cluster ranges and
stay visible), Lights Lab 6 %, PBR Lab 0.3 %.

Identity, culling off against on:

* lavapipe (deterministic): City Lab captures 0 differing pixels (59 % of the elements hidden), the 60-frame `city_pan`
  sequence 0 differing pixels.
* RTX temporal sequences (60 frames): Bistro exterior bit-identical (99 dB, max |d| 0), City Lab max |d| 1, Sponza max
  |d| 3 (its run-to-run floor); captures at each scene's floor (scattered 1-3 level noise, no object missing; PBR Lab,
  which hides almost nothing, shows the same noise).

Performance, in one process (`m6_f79_occ_ab`, blocks of 30 frames; the culling's own GPU work counted in the opaque
pass's region): Bistro exterior frame −0.9 % (opaque pass −0.038 ms), City Lab **+4.2 %** (+0.092 ms), Bistro interior
+0.2 %, Sponza +0.4 %; render thread +0.09 ms in Bistro exterior (indirect draws record more slowly). The culling's
GPU cost (pyramid in 11 dispatches from a 1024 × 1024 level 0; long instanced runs compacted by one workgroup each,
serially) was larger than what it saved.

v2 (`fellow78`, `d43a07c`): pyramid in two dispatches (levels 0-5 per 64 × 64-pixel block, then level 6) from a level 0
padded only to multiples of 64 (960 × 576 at 1080p); the test a thread per list element; runs drawn in chunks of up to
64 elements (one indirect draw per chunk, in order), compacted a thread per chunk. Measured in `m6_f82_occ_ab`.

## Decision

(pending: `m6_f82_*`)
