# 0249 — Hair on a head's mesh, and fewer, wider strands far away

Status: **KEEP** (`FellowHair3D.scalp_mesh`, none by default: the sphere's cap of decision 0248; `lod_distance` 4 m,
`simulation_distance` 40 m on by default) · Branch `fellow/geometry` · Code: `modules/fellow_fluid/fellow_hair_3d.{h,cpp}`
(`_scalp_mesh_triangles()`, `_scalp_mesh_sample()`, `_update_lod()`) · Checks: `benchmarks/project/bench/hair_lod_check.gd`
(lavapipe), `bench/hair_check.gd` (decision 0248's, unchanged), the regression scene `hair`.

## Problem

Decision 0248's hair grows on a sphere's cap: a character's head is not a sphere, its hairline not a cone's. And the
strands cost the same at 1 m and at 50 m: on lavapipe the hair's frame at 8 and 16 m was 773 and 611 ms against 782 at
1 m — its ten thousand strands, 24 quads each, drawn whatever their size on the screen. Its report's "next".

## Change

* **`scalp_mesh`**: a mesh in the node's space (the head's, or a scalp made for it); the roots grow on its faces whose
  three vertices' colour's red is 0.5 or more (every face if it has no colours) — by area: the guides spread over them
  by farthest-point sampling among sixteen random candidates a guide, the strands at random; the mesh's normals start
  each guide's comb. The sphere stays the head's collider and the styled shape's (fit it inside the head).
* **`lod_distance`** (4 m): beyond it a share of the strands is drawn, (lod_distance / d)², at least `min_strand_share`
  (5 %), each drawn wider by the inverse share so the hair covers as much — at a pixel's width the strands' coverage is
  their alpha (decision 0248), so fewer strands of more coverage dither to the same. d is the camera's distance as
  through Godot's default 75-degree view (a narrower view counts as nearer). The roots are in random order: any first
  share covers the scalp; the MultiMesh's visible instance count draws it.
* **`simulation_distance`** (40 m): beyond it no simulation; the styled shape follows the head.

## Verification

`bench/hair_lod_check.gd` (lavapipe, 960 × 540, TAA):

* An egg-shaped head (an ellipsoid of 0.085 × 0.11 × 0.1 m, 48 × 24 faces) painted above the ears and off the face:
  160 guides, their roots within 0.41 % of the ellipsoid's surface (the faces' flatness), none on the face, none under
  the ears; hair over the top and sides (picture). First version: a face counted when two of its vertices were painted —
  a root on the face and two under the ears; now all three.
* The strands drawn and lavapipe's GPU time (the frame; relative only):

| camera | strands drawn | width × | simulated | GPU, with | GPU, every strand |
|---:|---:|---:|---|---:|---:|
| 1.0 m | 10 000 | 1 | yes | 782 ms | |
| 4.1 m | 9 592 | 1.04 | yes | 734 ms | |
| 8.2 m | 2 398 | 4.17 | yes | 259 ms | 773 ms |
| 16.3 m | 600 | 16.7 | yes | 127 ms | 611 ms |
| 32.7 m | 500 (5 %) | 20 | yes | 113 ms | |
| 61.3 m | 500 | 20 | no | 121 ms | |

  At 8 m the hair with and without the level of detail looks the same (pictures).
* `bench/hair_check.gd` as before (its camera within 4 m); the regression scene `hair` the same as its golden.

**On the laptop** (milestone 131, job f466: RTX 5070 Ti Laptop, 1920 × 1080, the release template; each distance
settled 60 frames, the median of 120; three runs): the frame's GPU time

| camera | strands drawn | GPU, with | GPU, every strand |
|---:|---:|---:|---:|
| 1.0 m | 10 000 | 0.88-0.90 ms | |
| 4.1 m | 9 592 | 0.75-0.80 ms | |
| 8.2 m | 2 398 | 0.36-0.42 ms | 0.86-0.96 ms |
| 16.3 m | 600 | 0.27-0.32 ms | 0.67-0.71 ms |
| 32.7 m | 500 | 0.26-0.31 ms | |
| 61.3 m | 500, not simulated | 0.29-0.34 ms | |

The scene without the hair's strands is about 0.27 ms here (32 m), so the hair costs about 0.6 ms at 1 m and 4 m,
0.1 ms at 8 m with its level of detail (0.6 ms without), and nothing measurable from 16 m (0.4 ms without). A first
job (f464) measured five frames a distance, the GPU's clocks still changing: 0.9-2.4 ms at 1 m, the two runs apart by
up to twice.

## Decision

Keep: hair on a head's own shape and hairline, and a far head's hair at a fifth of its cost.

## Rejected / not done

* **Fewer guides far away**: the simulation is the CPU's 0.3 ms; past `simulation_distance` it stops.
* **Fewer segments a strand far away**: the strip is one mesh for all strands; the share of strands is the larger
  saving.
