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

## Result

(pending: `m6_f77_occ_ab*`)

## Decision

(pending)
