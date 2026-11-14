# 0228 — Cloth kept off its own triangles

Status: **KEEP** (`FellowCloth3D.self_collision_triangles`, off by default) · Branch `fellow/geometry` · Code:
`modules/fellow_fluid/fellow_cloth_3d.{h,cpp}` (`_find_self_tri_pairs()`, `_substep()`) · Checks:
`benchmarks/project/bench/cloth_check.gd` (lanes `self_heap_thin`, `self_heap_tri`, `--many=N --self=1 --self_tri=1`), the
regression scenes `cloth` and `cape`.

## Problem

Decision 0221 keeps a cloth's particles apart from each other; between a layer's particles lie its triangles, and a
particle of another layer passes between two of them unless the distance is about 0.7 of the cloth's cells or more —
decision 0221 uses three quarters of the mean edge (8.5 cm on a sheet of 10 cm cells), so layers lie that far apart, a
visible thickness. With a thinner distance the layers pass through each other between the particles (below: 71-167 of
the heap's 475 particles within half the distance of another layer's triangle). Middleware keeps cloth off its own
triangles (point-triangle self-collision) for thin layers.

## Change

`self_collision_triangles` (off), with `self_collision`; the distance then defaults to a quarter of the mean edge:

* **Once a frame** (the worker thread, after decision 0221's pairs): each live triangle's bounds grown by the frame's
  reach (twice the distance) are hashed into every cell they cover (cells twice the larger of the reach and the mean
  edge: fewer cells a triangle, the bounds test sorting the extra candidates); each particle looks in its own cell and
  keeps the triangles that are not its own, whose bounds hold it, that are within the reach now and were not within it at
  rest (the fabric's own neighbourhood) — with the side of the triangle it is on.
* **In each substep** (after the particle pairs): a particle nearer than the distance to such a triangle, on its side, is
  pushed out to the distance along the triangle's normal, the particle and the triangle's corners each by its inverse
  mass and its weight at the projection (a point-triangle constraint); not where the projection falls outside the
  triangle (its neighbour's, or a particle pair's). A particle that crossed in the substep is brought back to its side.
* `get_stats()`: `self_tri_pairs`, `self_tri_contacts`.

## Checks (cloud: the editor build and the release template the same, timings aside)

The earlier lanes of `cloth_check.gd` the same (the option off); the regression scenes `cloth` and `cape` exact. New
lanes: decision 0221's heap in still air with the distance a quarter of the mean edge (2.8 cm), by particles only and
against the triangles too. At 4 s, over the particles not within 1.5 cells at rest of a triangle's corners (the grid's
triangles as the cloth splits them):

| | nearest two particles | particles within half the distance of another layer's triangle | nearest such |
|---|---:|---:|---:|
| `self_heap` (decision 0221, 8.5 cm) | 8.47 cm | 0 | 6.19 cm |
| `self_heap_thin` (particles only, 2.8 cm) | 2.76 cm | 71 | 0 (through) |
| `self_heap_tri` (and the triangles, 2.8 cm) | 3.32 cm | 0 | 2.55 cm |
| `self_heap_off` | 0.65 cm | 130 | 0 |

On lavapipe's pictures the thin heap's folds cross each other; with the triangles they lie on each other. Over 10 s the
heaps settle (the triangles' to 0.1 cm/s of motion within it, the particles' only to 0.4-0.5 cm/s: particles held
through a layer pushed back and forth).

## Cost

64 flags in the wind (`--many=64`, the release template, cloud, 2 cores; the worker threads' time a frame): without
self-collision 38.2-39.4 ms, particles 45.5-47.7 ms, and the triangles 52.4-53.2 ms (+0.1 ms a flag of 400 particles over
the particles alone). The triangles in the cells of their bounds (a cell the larger of the reach and the edge) cost
55.4-56.3 ms, cells four times that size 57.6-58.0; without the bounds test 61.3-64.4.

## Decision

Keep, off by default: layers as thin as a quarter of an edge without passing through each other, for the scenes that
fold cloth on itself (a heap, a cape over a body); the particles alone stay the cheaper choice for thick fabric.

## Rejected / not done

* **Edges against edges** (two layers' edges crossing where no particle is near a triangle): the particle pairs and the
  triangles' neighbours catch them in practice at this distance.
* **Continuous collision**: as decision 0221, the frame's reach is the margin.
