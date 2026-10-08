# 0029 — Depth pre-pass: simple elements first

Status: **REJECT** (left in, off) · Branch `fellow/geometry` · Toggle `rendering/fellow/geometry/prepass_simple_first`
(runtime, off) · Code: `render_forward_clustered.cpp` (`_render_list_template`, `fellow_depth_filter`).

## Problem

The depth pre-pass draws the opaque list in its sorted order, where elements with the shared depth material (no
discard, no vertex function: one depth-only pipeline) and elements with their own depth shaders (alpha scissor,
vertex animation) are interleaved. An alpha-tested element drawn before the walls in front of it runs its fragment
shader (texture fetch, discard) on pixels that a later wall covers: early depth testing only rejects fragments behind
what is already drawn. Sponza's pre-pass took 1.36-1.39 ms, Bistro exterior's 0.70 ms (`m6_f83_all_ab`).

## Change

With the toggle the pre-pass draws its list twice: first only the elements with the shared depth material, then only
the others (each run in the list keeps its order). Same depth buffer; the normal/roughness buffer can differ where
surfaces tie in depth (coplanar), since the later of two equal-depth fragments passes the GREATER_OR_EQUAL test.

## Measured (`fellow80`, `m6_f84_*`)

Images against the stock order: lavapipe PBR Lab captures 0 differing pixels; RTX captures City Lab and Lights Lab
bit-identical, Bistro exterior 93 dB, PBR Lab 78 dB, Sponza 73 dB, Bistro interior 71 dB (min 69 dB; ties between
coplanar surfaces); temporal sequences Bistro exterior and City Lab bit-identical, Sponza 75 dB, Lights Lab 94 dB.

In one process (`m6_f84_prepass_ab`, blocks of 30 frames, 4 rotations):

| scene | depth pre-pass | opaque pass | frame | render thread |
|---|---:|---:|---:|---:|
| Sponza | 1.390 → 1.390 ms | +0.004 ms | +0.0 % | −0.4 % |
| Bistro exterior | 0.702 → 0.696 ms | +0.002 ms | −0.1 % | **+2.5 %** (p 1e-9) |
| Bistro interior | 0.164 → 0.163 ms | −0.002 ms | +0.0 % | +0.6 % |
| PBR Lab | 0.120 → 0.120 ms | +0.004 ms | +0.3 % | −0.5 % |

## Decision

REJECT. The pre-pass costs the same in either order: the benchmark scenes have almost no alpha-tested geometry in the
pre-pass (Sponza's glTF materials are all opaque or blended; its 1.39 ms pre-pass is 12.8 million triangles of vertex
work, which no order changes), and the second walk over the list costs the render thread 2.5 % in Bistro exterior.
Left in the code, off; a scene full of alpha-tested foliage in front of opaque walls is where it could matter.
