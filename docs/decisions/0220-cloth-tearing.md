# 0220 — Cloth that tears

Status: **KEEP** (`FellowCloth3D.tear()` and `tear_strain`, 0 by default: nothing tears unless a scene asks) · Branch
`fellow/geometry` · Code: `modules/fellow_fluid/fellow_cloth_3d.{h,cpp}` · Checks:
`benchmarks/project/bench/cloth_check.gd` (five new lanes, `--many=N --tear_strain=X`), the regression scenes `cloth`
and `cape`.

## Problem

The readiness assessment's physics row: "cloth without self-collision or tearing". A banner a car drives through, a
flag shot through, a curtain cut by a blade, a sail ripped by a storm: AAA scenes tear cloth (Havok Cloth and Chaos
Cloth break constraints past a limit; games cut holes where a shot passes). Decision 0181's cloth could only be let go
of its pins.

## Change

* **`tear(position, radius)`**: every edge of the cloth passing within the radius of the point breaks (a shot; a cut is
  a line of calls). Returns the edges broken.
* **`tear_strain`** (0: off): after each frame's substeps, a stretch edge longer than (1 + `tear_strain`) × its length
  at rest breaks — a sheet held at its corners that something pushes through tears where it is pulled most, and the
  tear runs on while it is pulled. Edges between two pinned particles are not checked (not in the solver).
* **What breaking does**: the broken edge's triangles go — degenerate in the mesh's index buffer (uploaded once on the
  main thread: `mesh_surface_update_index_region`, the vertex count and the blend shape's buffers unchanged), and out
  of the wind's and the normals' lists; with them every edge left without a triangle (no invisible threads holding the
  pieces) and every bending pair across a gone triangle. A particle of no triangle left is left where it is and out of
  the bounds. The tethers (decision 0181: each free particle to its nearest pin along the cloth) are found again along
  the edges left — a part cut from every pin has none and falls free; a part still hanging by a strip is held by the
  path round the hole. A part fallen 100 m below the cloth's node is dropped (its triangles gone), so a piece falling
  with nothing under it does not grow the cloth's bounds for ever.
* The topology kept for it: each triangle's three edges, each edge's triangles (any number: non-manifold source
  meshes), each bending pair's two triangles (sorted with the pairs: the same comparisons, so the same order as before),
  each triangle's place in the index buffer.
* The signal `torn(position, edges)` (by `tear()` at once; by `tear_strain` after the frame's simulation, on the main
  thread), `get_torn_edge_count()`, `get_stats()`' `torn_edges`, `triangles_all`, `loose_particles`; `reset()` makes the
  cloth whole again (the tethers as built).

## Checks (cloud: the editor build and the release template the same; GodotPhysics3D and Jolt the same)

The earlier eleven lanes of `cloth_check.gd` the same values as before the change (bit for bit); the regression scenes
`cloth` and `cape` exact. The new lanes (4 s at 60 fps):

| lane | what happens | result |
|---|---|---|
| `tear_cut` | a curtain 2 × 2.4 m (top edge pinned) hanging still; `tear()` at its middle, radius 0.25 m, at 1.1 s | 84 edges broken, 64 of 960 triangles gone, 21 particles loose (the hole's inside); one signal, at the middle within 1 cm; the curtain still hanging whole round the hole (its bottom 0.59 m up, as an untorn one's) |
| `tear_reset` | the same, `reset()` at 2.5 s | whole again: no edge torn, 960 triangles, hanging as before (strain 0.7 %) |
| `tear_strain` | a sheet 2 × 2 m held at its four corners; a ball (radius 0.3 m, a body moved by the script) through its middle at 8 m/s; `tear_strain` 0.5 | the sheet pushed 0.73 m back before it tore (at 1.23 s, at the ball's height and the middle within 1 cm), 24 edges, 23 triangles; a hole torn open, the ball through, the sheet on its corners |
| `tear_strain_off` | the same with `tear_strain` 0 | nothing torn (the largest strain 15 %): the ball goes through between the particles, as before the change; the sheet back on its corners |
| `tear_off` | a flag on a pole in a breeze (`wind_scale` 0.3) cut between its 4th and 5th columns at 1.1 s (16 calls, one a row, radius 2 cm) | 16 edges broken and the 15 diagonals between them left without a triangle (a clean cut: 30 triangles); the cut part on the ground at 3.9 s (its highest particle 0.18 m up), the strip on the pole still hanging (its bottom 2.8 m up) |

With `--pictures=1` on lavapipe the strip's three new rows: the curtain's round hole, the sheet's star-shaped tear
where the ball went, the flag's cut part lying on the ground.

## Cost

Nothing when nothing tears (the lists are the same; one pointer test a substep for loose particles). `tear_strain`:
one pass over the stretch edges a frame — 64 flags in the wind (`--many=64`, cloud, 2 cores): 26.2-27.2 ms of the
worker threads a frame without, 27.1 with 0.5 (the same within the noise). A tear: the tethers' shortest paths again
(a few hundred particles: well under a millisecond) and one index upload.

## Decision

Keep: a cloth tears where it is cut or pulled too far, and its cut parts fall.

## Rejected / not done

* **Splitting particles along the tear** (a clean edge on each side, no particle lost): needs new vertices and a
  rebuilt mesh; the hole costs one row of triangles (a cell, 4-8 cm at the default resolution).
* **Tearing by the bending constraints or by speed**: the stretch is what pulls cloth apart.
* **Self-collision**: still not done.
