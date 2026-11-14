# 0221 — Cloth kept off itself

Status: **KEEP** (`FellowCloth3D.self_collision`, off by default) · Branch `fellow/geometry` · Code:
`modules/fellow_fluid/fellow_cloth_3d.{h,cpp}` (`_find_self_pairs()`, `_substep()`) · Checks:
`benchmarks/project/bench/cloth_check.gd` (two new lanes, `--many=N --self=1`), the regression scenes `cloth` and
`cape`.

## Problem

The readiness assessment's physics and characters rows: cloth "without self-collision". Decision 0181's cloth collides
with shapes, not with itself: a sheet falling in a heap lies through its own layers, a cape folding over a moving body
passes through its folds — the layers flicker through each other on screen. Middleware keeps cloth off itself by its
particles (NvCloth's and Chaos Cloth's self-collision: spheres on the particles, a spatial hash).

## Change

`self_collision` (off) and `self_collision_distance` (m; 0: three quarters of the cloth's mean edge — 8.5 cm on a
1.8 × 2.4 m sheet of 24 cells):

* **The pairs, once a frame** (on the worker thread, before the substeps): the particles hashed into cells twice the
  distance across (a table of twice the particles' count, chained); each particle looks at its own cell and half of the
  26 around it (each pair of cells once), and keeps the pairs within twice the distance now whose particles are not
  within twice the distance at rest — a neighbour's own constraints keep it (the edges, the diagonals and the bending
  pairs are all nearer at rest). A bucket may hold several cells: a particle is taken only from the cell looked at.
  Torn-loose particles (decision 0220) and pairs of two pinned particles are left out.
* **In each substep** (after the stretch, bending and tether constraints, before the colliders): a pair closer than the
  distance is pushed apart along its line to the distance, each particle by its share of the pair's inverse mass (two
  on top of each other: along the cloth's normal).
* Twice the distance as the frame's reach is the margin for the particles' motion within the frame (a particle moving
  more than the distance in a frame may pass a layer).
* `get_stats()`: `self_pairs`, `self_contacts` (pushed apart in the frame's last substep), `self_distance`.

## Checks (cloud: the editor build and the release template the same; GodotPhysics3D and Jolt the same)

The earlier lanes of `cloth_check.gd` bit for bit the same (self-collision off); the regression scenes `cloth` and
`cape` exact. The new lanes: a sheet 1.8 × 2.4 m hung by its top edge, turned 30° to the breeze (`wind_scale` 0.3),
let go at 0.5 s; it falls in a heap. At 4 s, over every two particles that are not within 1.5 cells of each other at
rest:

| | nearest two | particles with another within half the distance | the heap's height |
|---|---:|---:|---:|
| `self_heap` (self-collision, 8.5 cm) | 8.47 cm | 0 | 0.25 m |
| `self_heap_off` | 0.65 cm | 56 | 0.29 m |

On lavapipe's pictures the heap lies in folds one over another; without, the folds cross through each other.

## Cost

64 flags in the wind (`--many=64`, cloud, 2 cores; a flag's folds give it pairs too): the worker threads 26.4-27.4 ms a
frame without, 32.5-33.0 ms with (+22 %, 0.09 ms a flag of 400 particles). Looking at all 27 cells around each
particle cost +33 %; with a check for buckets seen twice instead of the cell's own particles, +50 %.

## Decision

Keep: a cloth that folds onto itself, falls in a heap or wraps a body keeps its layers apart.

## Rejected / not done

* **Particles against triangles** (a particle kept off the other layer's triangles, not only its particles): thinner
  layers without the particles slipping between, at several times the cost; the particles' distance is kept at
  three quarters of an edge instead.
* **Continuous collision** (a particle's path in the substep tested against the other layer): a layer passed in one
  frame is not caught; the frame's margin is the distance.
* **Friction between layers**: the folds slide on each other.
