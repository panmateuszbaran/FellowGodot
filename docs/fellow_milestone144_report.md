# FellowEngine — milestone 144 report (hair shadowing itself)

Status: **FINAL** (2026-11-18). Branch `fellow/geometry`. Lavapipe in the cloud (the laptop's GPUs on milestone 143's
measurements meanwhile).

## 0. Result

* **Hair lit as a volume** (decision 0268, opt-in per node; on in the editor's "Add Hair" preset):
  `FellowHair3D.self_shadow` shadows the hair from a directional light by itself. Each frame its guides' points are
  splatted into a deep opacity map across the light on the CPU (cells twice the guides' spacing, 32 slices along the
  light; what each point's strands cover of a cell, by its segment's sine to the light), each point's share of the
  light is `exp(-self_shadow_density × the strands in front of it)`, and the strands dim that light's diffuse, specular
  and transmitted light by their three guides' shares.
* hair_check's head lit from behind: from the side the hanging hair under the top layers in their shade, its depth
  visible instead of one glowing sheet; the points' mean share 0.27 at the default density (0.5).
* About 75 µs a head a frame, on the worker threads with the simulation (24 heads: 1.8 ms summed, the main thread's
  hair group 3.5 → 4.2 ms on the cloud's 2 processors). Without it the material's code and the pictures as before.

## 1. Checks

`bench/hair_check.gd --self_shadow=1` (a new check: a light found and part of the hair in its own shade),
`bench/hair_crowd_check.gd --self_shadow=1`; the image regression suite (48 of 48 identical: its hair scene keeps the
option off); the editor check (the preset added without warnings, undone).

## 2. Next

Milestone 143's measurements on the laptop (running): Frontier Lab's levels of groups, the Intel GPU's terrain holes
and City Lab, milestone 33's device loss asked again.
