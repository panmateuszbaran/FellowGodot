# 0026 — Occlusion culling against a depth pyramid

Status: **MEASURING** (statistics only) · Branch `fellow/geometry` · Debug setting `rendering/fellow/debug/occlusion_stats`
· Code: `shaders/effects/fellow_hzb.glsl`, `shaders/effects/fellow_occlusion_test.glsl`, `render_forward_clustered.{h,cpp}`
(`_fellow_occlusion_stats`), `fellow_settings.*`.

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

## Decision

(pending)
