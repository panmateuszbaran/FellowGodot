# 0355 — An osteria open to the square, its room furnished and peopled

Status: **KEEP** · Branch `fellow/geometry` · Code: `showcase/scripts/rooftops_osteria.gd` (new, on CasaRosa);
`showcase/scripts/rooftops_crowd.gd` (its people, its shelter); `showcase/rooftops/rooftops.tscn`,
`tools/build_rooftops.gd` (the node); `tools/climb_look.gd` (`--program=osteria`, `--view=door|inside|walkin`).

## Problem

The owner: "and maybe some open buildings with a real interior?" Every building on the square was a shell: its doors
and shop windows painted wood or glass with a room mapped behind it (`FellowInteriorMaterial3D`, milestone 188), its
ground floor one solid collider.

## Change

CasaRosa's ground floor (the south side's palazzo, 10.2 by 9.2 m, its two shop arches on the square) is an osteria,
made at run time by a node on the building (`rooftops_osteria.gd`, in the building's frame):

* **Opened**: the arches' shop glass taken out of a copy of the building's mesh (the front's glass triangles from the
  ground up at the reveals' depth: two arches found, 2.6 and 2.4 m wide, 3.4 m high); the ground floor's solid
  collider turned off and the room's walls, the front round the arches and the ceiling made instead.
* **The room** (9.5 by 8.4 m, 4 m high, on the pavement's collider 8 cm up): a floor of 30 cm terracotta tiles,
  plastered walls on a stone skirting, the front's inside round the arches (their spandrels as the building generator
  makes them), a plank ceiling on beams every 1.05 m and two wall plates — the town's own shaders (masonry, plaster,
  wood) on metric UVs.
* **The fireplace**: a stone hearth, jambs and mantel, a sooty opening, a plastered hood up to the ceiling; andirons and
  three logs burning (`FellowBurnable3D`) in a gas volume closed at its back, sides and hearth (`FellowGasVolume3D`:
  the flames rise into the hood; its fire light, with shadows, lights the room).
* **The counter**: a wooden counter, three shelves of bottles (green, brown, clear glass), jugs and plates behind it,
  bottles and cups on it; three barrels on a rack in the corner, one standing at the counter's end.
* **The tables**: four, with benches 0.47 m high (the sitting clip's), jugs, cups of wine, plates with bread and a
  candle each (its flame, a small light flickering); an iron chandelier of six candles over the middle (one light, with
  shadows); hams and garlic from a beam over the counter.
* **Its people** (`rooftops_crowd.gd`): the host behind the counter, ten guests at the tables, two at the counter, one
  warming at the fire — 14, looking, talking (E) and run into as the square's.
* **Its shelter**: in the rain the square's people go in through the arches (when a place is free and the door nearer
  than their street), stand inside out of it, and come out again after.

## Measurements

| | |
|---|---|
| geometry | 26,324 triangles in 46 mesh instances (the room's one per material, the candles' flames, the logs); 23 box colliders |
| lights | 6: the fire's (shadows), the chandelier's (shadows), four candles' (the chandelier's six candles share its light) |
| its fire (RTX, job f597) | power 2.4-2.7 after 7-8 s, the logs burning |
| the player walking in (`--view=walkin`) | through the arch from the square into the room, 8.4 m in 5 s, onto the floor at 0.08 m |
| the rain (`--view=door`, rain from 1 s) | 6 of the square's people inside, 9 gone by the streets, at 20 s |
| the frame inside (RTX laptop, 1280x720, `--view=inside`) | GPU 6.46 ms, render CPU 2.91 ms (without the room 4.64 and 1.50) |

* The demo's path unchanged within 0.1 mm (the whole demo: the building's colliders changed).

## Decision

KEEP: the osteria.

## Rejected / not done

* Rebuilding CasaRosa in Blender with an open ground floor: the building generator, the town's import and the scene's
  colliders rebuilt for one room; the node opens it at run time from the building as it is.
* The interior's own GI probe or lightmap: SDFGI and the lights do it; a baked lightmap would not follow the fire.
* Doors to open: the arches are open shop fronts.
