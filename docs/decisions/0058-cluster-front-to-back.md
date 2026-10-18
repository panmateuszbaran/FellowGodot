# 0058 — Large surfaces' clusters drawn front to back

Status: **REJECT** (left in, off; measured with `fellow143`) · Branch `fellow/geometry` · Setting `rendering/fellow/geometry/cluster_front_to_back`
(runtime, triangles per ordered range; 0, off, until measured) · Code: `storage_rd/fellow_mesh_clusters.*`
(`cull_ordered`), `render_forward_clustered.*` (`fellow_cluster_eye`, `fellow_cluster_f2b`), `fellow_settings.*`.

## Problem

Sponza's depth pre-pass takes 1.06-1.24 ms (`m11_f157`, `m11_f161`), as much as its transparent pass. Its surfaces are
few and large (one per material of the building, the curtains, the ivy wall, the trees' bark: 276 draws for 6.5 million
primitives), and the pre-pass draws them with the materials' depth shaders, which sample the normal and roughness maps
for the normal-roughness buffer the screen-space effects read. Mesh clusters (0007) draw such a surface's visible k-d
nodes in the tree's order, which does not depend on the camera: within a surface, a fragment that a nearer part of the
same surface will cover later is shaded and written all the same. 0026 found the vertex work of these passes small on
this GPU; what the order can change is the fragments that pass the early depth test.

## Change

`geometry/cluster_front_to_back` N: the depth pre-pass, the opaque pass and the motion pass visit a clustered surface's
k-d tree with the child on the camera's side of each split first, and split nodes entirely in view further while they
hold more than N triangles, so the surface is drawn as ranges from the nearest to the farthest. Not for surfaces drawn
with blending (their order-preserving clusters keep their order), not for orthographic views, not for shadow passes
(depth only, no fragment shader to save). More draws: the ranges of a surface are no longer one merged range.

The depth test keeps the nearest fragment whatever the order; fragments at exactly the same depth from two triangles of
one surface (coplanar overlaps) resolve by order, as they already do with 0007's tree order.

## Verification

lavapipe, Stress Lab captures (its floor, height field and moving mesh are clustered), 1024 triangles: **0 differing
pixels**; 3.7 times the cluster draws.

## Measured

In-process A/B (`m12_f169_f2b_ab`, autotune off, 4 × 4 blocks, profiled), 4096 and 16 384 triangles per ordered range
against the tree's order:

| scene | depth pre-pass (ms) | opaque pass (ms) | GPU |
|---|---|---|---|
| Sponza | 1.090 → 1.069 / 1.074 | 1.640 → 1.651 / 1.647 | −0.2 % / −0.2 % (n.s.) |
| Bistro exterior | 0.584 → 0.583 / 0.583 | 1.408 → 1.406 / 1.406 | −0.1 % / −0.0 % (n.s.) |
| Foliage Lab | 0.502 → 0.501 / 0.501 | 0.823 → 0.826 / 0.825 | +0.0 % (n.s.) |
| PBR Lab | 0.106 → 0.106 / 0.106 | 0.686 → 0.686 / 0.686 | −0.1 % (n.s.) |

## Decision

REJECT (left in, off). Front to back takes 2 % off Sponza's pre-pass and nothing off the frame: the pre-pass is not
spent on fragments that a nearer part of the same surface covers later. What it is spent on is the next question: its
millions of small triangles (the curtains' 2 million, the ivy's 4.9 million, the trees' bark), each covering a few
pixels or less, cost raster and quad work whatever their order.
