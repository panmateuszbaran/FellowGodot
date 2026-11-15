# 0235 — Cloth edges kept off each other

Status: **KEEP** (`FellowCloth3D.self_collision_edges`, off by default) · Branch `fellow/geometry` · Code:
`modules/fellow_fluid/fellow_cloth_3d.{h,cpp}` (`_find_self_edge_pairs()`, `_cloth_segments_closest()`, `_substep()`) ·
Checks: `benchmarks/project/bench/cloth_check.gd` (lanes `self_cross_tri`, `self_cross_edges`, `self_heap_edges`; the 64
flags' cost), the regression scenes `cloth` and `cape`.

## Problem

Decisions 0221 and 0228 keep a cloth's particles off each other and off its other layers' triangles. Two edges can still
pass through each other where neither has a particle near the other's triangles: two coarse layers crossing at an angle,
a towel falling across a bar of the same cloth between its particles. Cloth collision handling tests edges against edges
for it (Bridson, Fedkiw and Anderson's robust treatment of cloth collisions, 2002).

## Change

`self_collision_edges` (with `self_collision`; the distance then defaults to a quarter of the mean edge, as with the
triangles): once a frame, the pairs of edges of no common particle nearer each other than twice the distance and not at
rest are found with the point on each where they come nearest and the direction between those points; in each substep a
pair nearer than the distance along that direction is pushed apart to it — the four particles by their inverse masses
and their places along the edges (an edge-edge constraint), with decision 0229's friction.

The search: two such edges have particles within the reach and the longest edge of each other. The particles that near
now are found in a grid of cells that size; a pair near each other along the cloth at rest (its edges' rest lengths,
within half again that) is its own fabric and skipped; the edges of the pairs left are tested pair by pair (each pair
once a frame, the pairs within the reach at rest skipped). A cloth that does not come near itself tests nothing. The
first version hashed every edge's bounds and tested each edge's neighbourhood: 64 flags +55 ms a frame of the cloud's
worker threads, then +65 ms with a bounding-sphere test, the cost being the fabric's own neighbours; testing only
particles near each other by distance at rest skipped a towel built 25 cm above a bar whose edges are 32 cm long — hence
the distance along the cloth.

## Verification

`cloth_check.gd`, a towel (two columns, rows 30 cm apart) dropped 25 cm across a bar of the same cloth (two rows 12 cm
apart, pinned at its ends), neither with a particle over the other's triangles where they cross, distance 3 cm, friction
1 between the layers (without it the towel slides off the bar: an unstable equilibrium):

| | the towel's crossing above the bar, at its lowest after landing | at 4 s | edge pairs (last frame) |
|---|---:|---:|---:|
| particles and triangles (decision 0228) | through | 7.47 m below (falling) | 0 |
| and edges | 3.06 cm | 3.13 cm | 17 |

The heap in still air at 2.8 cm (decision 0228's lane) with the edges too: no particle within half the distance of
another layer's triangle, as without; 1 287 edge pairs in its last frame, 3.0 ms of simulation against 1.4; it settles
(0.64 cm/s against 13). The earlier lanes the same; the regression scenes `cloth` and `cape` exact.

Its cost (64 flags of 400 particles in the gale, particles and triangles on, the release template, the cloud's worker
threads, two alternating runs each): 40.1-40.2 ms a frame without the edges, 56.6-57.5 ms with them — about a quarter of
a millisecond a flag where flags touch themselves, against a tenth for the triangles.

## Decision

Keep, off by default: what decision 0228 leaves through — edges crossing — kept apart, for a cloth whose layers cross at
angles or whose particles are far apart, at its cost.
