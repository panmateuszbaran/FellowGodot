# 0268 — Hair shadowing itself: a deep opacity map of the guides on the CPU

Status: **KEEP**, opt-in per node (`self_shadow` 0 by default: the material and pictures as before; the editor's
"Add Hair" preset turns it on) · Branch `fellow/geometry` · Code: `modules/fellow_fluid/fellow_hair_3d.{h,cpp}`
(`shader_code(p_self_shadow)`, `_prepare_self_shadow()`, `_self_shadow_step()`, `_self_shadow()`,
`_find_self_shadow_light()`), `modules/fellow_editor/fellow_editor_plugin.cpp` (the preset) · Properties:
`FellowHair3D.self_shadow`, `self_shadow_density`, `self_shadow_light` · Checks: `bench/hair_check.gd --self_shadow=1`,
`bench/hair_crowd_check.gd --self_shadow=1`, the regression scene `hair` (exact), the editor check (the preset).

## Problem

Hair is a volume: light from the sun lights its outer strands and reaches the inner ones through them, dimmer the more
strands it crosses. `FellowHair3D` (decisions 0248-0250) lit every strand as if nothing stood between it and the light
but the shadow maps — whose texels are wider than a head's locks, and which see the hair's strands as dithered
coverage — darkened its roots by a constant (`root_occlusion`) and its strands by a random layer
(`layer_occlusion`), whichever side the sun was on. Lit from behind or the side, a head of long hair glowed the same
through its whole depth: a flat orange sheet, the strands under others as bright as the ones on top. The readiness
review's characters row: "no self-shadowing beyond the light's shadow maps".

## Change

**The map** (on the CPU, each frame, after the simulation): the guides' points (160 guides × 13 points by default) in
a frame across the directional light — a grid of cells twice the guides' mean spacing at their roots, each cut into 32
slices along the light from the hair's side nearest it (a deep opacity map, as games draw on the GPU for hair). Each
point is splatted bilinearly into its four cells, into its slice, with what its strands cover of a cell: its guide's
share of `strand_count`, its segment's length, the strands' width and their tips' fade there (as drawn), and the
segment's sine to the light — a lock lying along the light hides little of itself, one across it a lot. Summed along
each cell's slices from the front, a point's optical depth is what lies in front of it in its cells (half a slice of
bias: no point shadows itself), and `exp(-self_shadow_density × depth)` the share of the light it gets. No sorting:
O(points) and O(cells × slices), about 75 µs for 2 080 points on one of the cloud's cores.

**On the worker threads**: the light (`self_shadow_light`, or the scene's first visible `DirectionalLight3D`, looked
for again at most once a second while there is none), its direction and the node's scale are read on the main thread
after the simulation's steps; then every node's map on the worker threads at once (WorkerThreadPool's group task, as
the steps, decision 0250; a single node's on the main thread); the main thread copies each point's share into the
guides' texture's fourth channel (unused until now) with its points. `get_stats()`: `self_shadow_usec`,
`self_shadow_lit`, `self_shadow_min`, `self_shadow_mean`; `get_global_stats()`: `self_shadow_usec` summed over the
nodes.

**The strands** (a shader variant made only with `self_shadow` above 0: without it the material's code is decision
0248's, character for character): each strand takes its three guides' shares at its position along them, weighted as
its points are; the light function dims the directional light the map was made for (its direction a uniform, matched
against `LIGHT`) — diffuse, both specular lobes and the light through the hair from behind — by
`mix(1, share, self_shadow)`. Other lights as before.

`self_shadow_density` 0.5 by default: a layer of strands that covers the light lets about 60 % of it through (strands
are translucent; dark hair absorbs more — higher; fair hair less — lower).

## Verification

* `hair_check.gd` (a head with 10 000 strands of 32 cm, the sun from behind its right, a fill light; lavapipe,
  960 × 540) with `--self_shadow=1`: every check as without (lengths, collisions, the turn's lag, the wind), and the new
  one — a light found and part of the hair in its own shade: the points' least share 2·10⁻¹¹, their mean 0.27 (0.43 at
  density 0.25, 0.16 at 1). The pictures: from the side the hanging hair under the top layers dark, its depth visible,
  the top layer lit; from behind the side away from the sun darker, the outer strands brighter than the inner ones.
* Its cost (headless, the cloud's CPU): 71-79 µs a frame for the head's map (a single node's on the main thread, as
  its step; several nodes' on the workers), beside its simulation and upload's 273-342 µs (the same with and without
  it, two runs each). The first version sorted each cell's points by depth: 1 ms a frame.
* `hair_crowd_check.gd` (24 heads of 2 000 strands, each turning; the cloud's 2 processors) with `--self_shadow=1`:
  the maps 1.83 ms a frame summed over the heads (76 µs each), the main thread's whole hair group 3.49 → 4.19 ms;
  every head stepped every frame, the lengths kept.
* The image regression suite: exact (its hair scene keeps `self_shadow` 0: the same shader, the fourth channel 1).
* The editor check: the "Add Hair" preset (now with `self_shadow` 1) added, without warnings, undone.

## Decision

Keep, opt-in per node: hair lit as a volume from the sun for a fraction of its simulation's cost, on the worker
threads; existing scenes unchanged; the editor's preset turns it on.

## Rejected / not done

* **Sorting each cell's points by depth** (the first version: exact depths, each point's guide's own points left out
  of its shade): 1 ms a frame on the main thread, most of it sorting and the guide's own points' overlaps; the slices
  and the segments' sine to the light give the same shade in a tenth of the time.
* **A deep opacity map on the GPU** (the strands drawn from the light into layered textures, as AAA hair renderers
  do): the guides' points are already on the CPU, a few thousand of them; the strands between three guides take their
  guides' shade smoothly. Per-strand detail (a single strand's shadow on its neighbour) is left to the shadow maps.
* **Other lights** (point and spot lights, a second sun): one map, the sun's; the others light the hair as before.
