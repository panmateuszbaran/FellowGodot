# FellowEngine — milestone 133 report (hand-overs per instance dithered)

Status: **FINAL** (2026-11-17). Branch `fellow/geometry`. Lavapipe in the cloud; the laptop's RTX 5070 Ti Laptop GPU
(job f472).

## 0. Result

* The hand-overs FellowEngine makes per instance in the MultiMesh culling's compute pass — a terrain's trees to their
  impostors (decision 0172), an HLOD's repeated meshes to their instanced proxy (0122) — can fade instead of
  switching (decision 0257): `fellow_instance_set_instance_range_margins()` draws a MultiMesh's per-instance range over
  a band on each side of its ends; the compaction writes each instance's fade after its data, and the scene shader
  reads it (under a new specialization bit, carried to the fragments in the instance index's high bits) and dithers it
  as decision 0256's fades, the side coming in keeping the other pixels.
* `FellowTerrainFoliage.impostor_fade` (0 m) for the trees; `FellowHLOD3D.fade` now also covers its instanced proxies
  (members fading by themselves, the proxy's instances fading in) and their impostors.
* No hole: the check's foliage (1 820 balls handing over at 40 m with 6 m of band) loses no pixel of the impostors'
  silhouettes, and the HLOD's instanced hand-over draws the same picture as its members alone, on lavapipe and the RTX.
* Frontier Lab with its trees fading over 20 m on each side of 140 m: frame 4.21 ms, GPU 3.77 ms, against 4.25 ms and
  3.81 ms switching — no measurable cost.

## 1. Checks

`bench/dithered_fade_check.gd` (sections 4 and 5: FellowHLOD3D merged and instanced; the foliage); Frontier Lab
`--frontier_impostor_fade=<m>`; the image regression suite (exact: nothing in it fades per instance).

## 2. Next

A streamed cell's content and its proxy still switch when the cell loads and unloads (milestone 134).
