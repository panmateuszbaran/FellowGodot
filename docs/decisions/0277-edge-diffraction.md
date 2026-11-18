# 0277 — Sound round an occluder's edge (edge diffraction)

Status: **KEEP** (opt-in: `FellowAcoustics3D.edge_diffraction`, off) · Branch `fellow/geometry` · Code:
`modules/fellow_world/fellow_acoustics_3d.{h,cpp}` (`_edge_path()`, `_edge_search()`, `_edge_heard()`;
`edge_diffraction`, `edge_reach`, `edge_diffraction_db`; `get_stats()`'s `edge_paths`) · Checks:
`benchmarks/project/bench/acoustics_check.gd` (four lanes, one under Movie Maker, `--edges=1` with `--many`) on
GodotPhysics3D and Jolt.

## Problem

Decision 0186 hears a source through what lies on the line to it, and decision 0207 through an opening a
`FellowAcousticPortal3D` marks. Anything else that blocks the line — a pillar, a car, a wall's end, a low wall, a
container — makes the source sound as if through a wall (−12 dB, its highs shelved), though in the world the sound
goes round it: a source behind a pillar 2 m wide is barely quieter, and is heard from the pillar's side. The readiness
assessment listed it: "paths through one opening, no diffraction round other edges".

## Change

With `edge_diffraction`, a source still more than half blocked and not heard through an opening:

* **The edge**: from where the line to it is blocked, rays along the line (0.6 m each way of the occluder's face) step
  out across it — to each side and up — at a tenth, a quarter, a half and all of `edge_reach` (4 m) until one passes
  the occluder; the edge between the last blocked and the first clear, halved three times; a point 0.15 m past it.
* **The path**: listener to that point and the point to the source, both clear; a thick occluder's far corner cutting
  the second leg moves the point along the line through it (0.5 m, 1 m). The shortest clear path of the three
  directions.
* **Heard that way** when louder than through the occluder, as an opening's path is (decision 0207): its extra length
  (the player's own attenuation, from the path's direction at its length), its bend — `edge_diffraction_db` (−9 dB)
  a quarter turn, in proportion — and its highs by the bend (a half turn: as through a wall).
* **Coherence**: the path found is tried first at the source's next tests (two rays) while both legs stay clear, and
  searched again every fourth; a search that found nothing waits four tests. A search costs up to about 45 rays,
  taken from `max_rays_per_frame` like the others.

Off by default: an existing scene sounds as before; the FellowEngine menu's "Add Acoustics" turns it on.

## Checks (cloud: the CPU; lavapipe for Movie Maker)

`acoustics_check.gd`'s new lanes (the listener at 1.7 m; GodotPhysics3D and Jolt the same, value for value):

| lane | expected | result |
|---|---|---|
| behind a pillar 2 m wide, 4 m high, at 4 m (source at 8 m) | round its side (+X): a bend of about 36°, occlusion 0.3, the highs' 0.2 | weight 1.0; 0.295, 0.197; from (2.64, 0, −7.97), 8.40 m |
| behind a wall 6 m wide, 2.2 m high | over its top (a bend of about 19°): 0.16 | weight 1.0; 0.166, 0.110; from above the line (1.45 m up at 8 m) |
| behind a wall 40 m wide, 12 m high | no edge within reach: through it | occlusion 1.0, weight 0 |
| the pillar, edge_diffraction off | through it | occlusion 1.0, weight 0 |

Under Movie Maker (`--listen=1`, the master bus captured), the source behind the pillar:

| | level | the share above 2 kHz | right over left |
|---|---|---|---|
| stock (no occlusion) | −12.8 dB | 62 % | 0.00 dB |
| the node, edge_diffraction off (through the pillar) | −40.0 dB | 2 % | 0.00 dB |
| **the node, edge_diffraction on (round its side)** | **−21.3 dB** | **46 %** | **+1.66 dB** |

Every earlier lane the same (the doorway's −25.8 dB, 30 %, +2.86 dB as in decision 0207).

## Cost

`--many` (512 rays a frame), the grid of 61 walls 4 m wide: with 64 sources every one tested each frame either way,
268 → 313 µs a frame (17.7 sources heard round an edge); with 256 sources the budget decides — 102 sources tested a
frame without, 47 with (13.3 heard round an edge each frame; the node's time within its spread, 420 → 355 µs: the
edge rays are short). Without the coherence (every test a search) 25 a frame. A scene of many blocked sources raises
`max_rays_per_frame` or keeps `edge_diffraction` for the near ones.

## Decision

KEEP, opt-in: a blocked source sounds round what blocks it, from that side.

## Rejected / not done

* **Maekawa's barrier attenuation** (10·log(3 + 20N), N the Fresnel number of the path's extra length): frequency
  true, but 15-24 dB for a modest detour — quieter than FellowEngine's −12 dB wall, so never chosen; the bend's dB
  as openings use keeps the two consistent.
* **Several edges in a row** (round a corner and then another), **edges of other shapes than walls and boxes'** (a
  sphere's silhouette is found as its nearest side, roughly): later work.
