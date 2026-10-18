# 0058 — Large surfaces' clusters drawn front to back

Status: **measuring** (`fellow143`) · Branch `fellow/geometry` · Setting `rendering/fellow/geometry/cluster_front_to_back`
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

Pending (`m12_f169_f2b_ab`).
