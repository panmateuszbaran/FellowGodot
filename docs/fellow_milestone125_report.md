# FellowEngine — milestone 125 report (hair on a head's mesh; fewer, wider strands far away)

Status: **FINAL** (2026-11-16). Branch `fellow/geometry`. Without the GPU (CPU and lavapipe). Milestone 124's hair grew
on a sphere's cap and cost the same at any distance.

## 0. Result

* **`scalp_mesh`** (decision 0249): the roots on a head mesh's painted faces (the three vertices' red 0.5 or more), the
  guides spread by farthest-point sampling, the comb started along the mesh's normals. An egg-shaped head: every root
  within 0.41 % of its surface, none on the face or under the ears.
* **Level of detail**: beyond `lod_distance` (4 m, through a 75-degree view) a share of the strands by the square of the
  distance, at least 5 %, wider in proportion so the hair covers as much: at 8 m 2 398 strands, lavapipe's frame
  773 → 259 ms; at 16 m 600, 611 → 127 ms; the hair at 8 m looking the same. Beyond `simulation_distance` (40 m) no
  simulation.

## 1. Checks

`bench/hair_lod_check.gd` (the roots, the strands drawn at six distances, the simulation's stop, pictures);
`bench/hair_check.gd` unchanged; the regression scene `hair` as its golden from both builds; the class reference
matches the binary; the editor check.

## 2. Next

The strands' GPU cost on the RTX (with and without the level of detail); grooming (part lines, styles); the guides on
worker threads for crowds.
