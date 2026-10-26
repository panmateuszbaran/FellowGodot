# 0120 — Burning objects: charring, burning through, ash (FellowBurnable3D)

Status: **KEEP** (a new node; without it nothing of it runs) · Branch `fellow/geometry` · Node `FellowBurnable3D`
(module `fellow_fluid`) · Code: `fellow_burn_sim.glsl`, `fellow_gas_burn.cpp` (the render thread), `fellow_gas_sim.glsl`
(`burn_inject[]`: the gas's EMIT, OBSTACLES and SPARSE_SOURCE read the objects), `modules/fellow_fluid/
fellow_burnable_3d.cpp` (voxelization, the display mesh, the material); reference `modules/fellow_fluid/tests/
burn_reference.py`, check `benchmarks/project/bench/burn_check.gd` + `tests/burn_compare.py`; bench scene Burn Lab
(`--scene=burn`).

## Problem

The user asked (2026-10-26) for objects that burn in the gas volumes' fire (0117) and deform with it by their volume
and density: wood that chars, burns through where it is thin, and leaves ash. Fire Lab's logs were static obstacles
under an emitter: the fire never touched them.

## Model (per voxel, `burn_reference.py`)

A burning object is a box of cubic voxels around its mesh (h: the longest side / `resolution`, two empty voxels
around), each with T (K above ambient) and the volume fractions of wood w, char c and ash a (the rest air). Per step
of burning time (the physics tick × `burn_speed`, 10 by default: a log burns in tens of seconds, not an hour):

1. **Conduction** between neighbours, the faces' conductivity their harmonic mean (wood 0.15, char 0.08, ash
   0.04 W/(m K); air does not conduct), explicit (stable: dt k / (c_v h²) is far below 1/6 at these sizes).
2. **Exchange with the gas** at the exposed faces (a face is open by 1 − the solid beyond − 0.6 × the ash beyond):
   convection (20 W/(m² K)) and radiation (ε 0.9, linearised about both temperatures) towards the gas's temperature
   outside the surface — the hottest of three samples along minus the solid's gradient at ½, 1 and 1½ sample
   distances (a gas cell, at least 1.5 voxels): the flame lies somewhere in front of the surface, and a cell next to
   it may be an obstacle cell (zero) — semi-implicit (stable at any step).
3. **Pyrolysis** above `pyrolysis_temperature` (573 K; the full rate `pyrolysis_rate` 0.05/s 120 K above): wood →
   char (0.85 of its volume, `char_yield` 0.25 of its mass), endothermic (200 kJ/kg); the volatiles (the other 75 %)
   are released as the gas's fuel. Where the voxel has an open face, their flame — thinner than a voxel, just
   outside it — returns `flame_feedback` (0.1) of their heat of combustion (15 MJ/kg) to it.
4. **Char oxidation** above `glow_temperature` (673 K, full 200 K above, `glow_rate` 0.02/s) where the char is
   exposed: char → ash (`ash_volume` 0.35 of its volume), exothermic (4 MJ per kg of char kept by the solid): the
   embers.
5. **Ash settles**: ash in a voxel with no wood or char left (< 0.05) falls into the voxel below while that has room
   (rate 2/s), down to the object's lowest layer — "below" along the voxel axis nearest to the world's down (a log
   modelled upright and laid on its side drops its ash downwards, not along its length).

Empty voxels take the gas's temperature. The heat capacity of a voxel is its materials' (wood 500 kg/m³ × 1700
J/(kg K), char ρ × yield / 0.85, ash 60 × 800), at least 5 % of solid wood's (nearly empty voxels stay stable).
A thin branch (a few voxels thick) heats through and burns away in the time a thick log only chars its skin: the
deformation follows the volume and the density through the heat budget, as the user asked.

## GPU (`fellow_burn_sim.glsl`)

Per object and step (after the gas volumes' steps of the same physics tick, in the tick's order: the objects read the
gas's temperature now, the gas reads their injection at its next step; `process_pending` sorts the volumes' and the
objects' queued steps by tick, then kind, then id): IGNITE (when `ignite()` was called), HEAT (1–4: the state
ping-pong, an emission texture RGBA16F — fuel released, the surface's temperature, its exposure, the solid), SETTLE
(5, and the display copy RGBA16F the material samples), INJECT (per gas cell of the region around the object: the
fuel and the surface over a footprint of 2 cells, 64 samples, the solid over the cell, 8), REDUCE every 6 ticks (the
totals, read back: the fractions, `ignited` / `burned_out`). The gas (0117) per object in its region: EMIT heats the
air towards the surface's temperature (`surface_heating` 200/s × the exposure; where fuel is released the
temperature of the voxels releasing it, elsewhere the exposed surface's), adds the fuel (`flame_strength` × 2.7 units
per kg of volatiles) and its smoke (`smoke` 0.05 per unit), and burns `flame_attachment` (0.5) of it at once where the
surface or the air is above the ignition temperature (the flame attached to the surface: the gas cells are coarser
than its stand-off); OBSTACLES marks the cells whose solid is above 0.5 as obstacles (the physics body of the object
is not one); sparse bricks (0119) count a region with fuel or a hot surface as a source.

The gas volumes got a **flame temperature limit** (`FellowGasVolume3D.flame_temperature_limit`, K above ambient):
the combustion makes no cell hotter, the fuel left burns further on where it is cooler. Without it the objects' fuel
ran the gas away (26 000 K above ambient in `burn_check.gd`'s campfire). By default it is 0 — none, the volumes as
before bit for bit (a default of 2200 K clipped Fire Lab's hottest cells: 64.5 dB) — and a volume burning objects feed
gets 2200 K when none is set. Burn Lab sets 1500 K.

## Drawing (the object's own material, extended)

The node replaces the mesh instance's mesh by a copy subdivided by conforming longest-edge bisection (the edges
marked by their positions: the copies of a vertex at a seam or between surfaces split together; no edge longer than
1.5 voxels, at most 300 000 triangles) and each material by a ShaderMaterial with the source's shader code (a
BaseMaterial3D's generated code, a ShaderMaterial's own) and parameters, plus:

* **vertex()**: from each vertex a march (half-voxel steps) along its stored direction (CUSTOM0: minus the initial
  fill's gradient at its position — the same for every copy of a position, so the surface cannot crack — the
  distance to the surface then, a reference fill) to where the fill (w + c + a) exceeds half the reference: the
  vertex moves by how far that surface has receded since the start (less a tolerance of 0.15 voxel). A vertex that
  finds no material is burned through there: it falls along the world's down onto what is left below it (the ash
  heap, the char), so the shell collapses onto the remains; with nothing below, every triangle touching it is
  discarded (moving such vertices to the material left drew stretched slivers, discarding all of them left a
  cage of strips). The normal follows the fill's gradient where the surface moved. The
  composition just inside the surface goes to the fragment.
* **fragment()**: darkened by char, greyed by ash (`char_color`, `ash_color`), rough; above ~750 K the char glows in
  a cellular crack pattern (`crack_scale` 2.5 cm; some cells brighter than others) and the hot ash softly
  (blackbody colour, `glow_intensity` × (T/1300)³, the temperature taken as at most 1300 K: the voxels sit in the
  flame and take its temperature, glowing charcoal is no hotter).

## Verification (lavapipe)

`burn_check.gd` against `burn_compare.py` (the reference run from the GPU's initial voxels), largest relative L2
difference of T, wood, char, ash:

| case | steps | difference |
|---|---:|---:|
| a log alone (24 voxels along it, no gas), ignited in its middle at 1100 K (with the surface flame's feedback) | 60 | 1.1 × 10⁻⁵ |
| a box on its side (the ash falls along its local x), ignited through, fast | 120 | 1.7 × 10⁻⁶ |
| the log over a campfire, one coupled step after 60 (the gas's temperature read back) | 1 | 9.2 × 10⁻⁷ |
| — its emission (half precision) and the gas cells' injection | 1 | ≤ 5.6 × 10⁻⁴ |

The gas: `gas_check.gd`'s campfire dense and sparse after 60 steps the same state bit for bit as before; with a flame
limit of 1500 K against `gas_compare.py` 3.1 × 10⁻⁶; Fire Lab's captures (three views) 0 differing pixels against
`fellow198`'s code.

## Calibration (Burn Lab, `burn_diag.gd`: the objects' fractions every second)

The coupling was tuned on Burn Lab (two logs and a branch over a kindling fire that burns 12 s, a crate lit at a
corner at 3 s; 48 gas cells, 32 voxels per object), in this order:

| change | what happened |
|---|---|
| 40 fuel units per kg of volatiles (the reference study's), no limit | the gas ran away: 8 200 K above ambient after 60 ticks, the log's voxels 6 500 K |
| 0.3 per kg | no runaway, but the logs went out with the kindling (12 s: 79 % wood left) |
| 0.3–9 per kg, the gas's flame limit 1500 K | bigger flames while the kindling burned; still out with it |
| the fuel's surface temperature from the voxels releasing it (not the exposed surface's average, 200 K above ambient: below the ignition temperature), `surface_heating` 20 → 200/s | with 9 per kg the flames outlived the kindling by 8 s; out at 25 s, 54 % / 27 % wood left |
| half the fuel burning where it is released (`flame_attachment` 0.5) | the logs burned to 10 % / 5 % wood, but in a fireball metres high (the gas's flame fed with the model's burning rate, about a hundred times real wood's per second of burning time) |
| the surface flame's heat back into the voxel (`flame_feedback` 0.1), 5.4 per kg | the logs burn on their own after the kindling to **5 % / 10 % wood, 4 % / 6 % char, 27 % / 25 % ash** (of the initial volume; 30 % is all of it as ash) at 70 s; the crate burns a sixth of its wood from its corner and goes out; but the flames lie on the ground, blown sideways at 5–9 m/s (the combustion's expansion of that much fuel: with the gas's `expansion` 0 the plume rises again) |
| **2.7 per kg** (1 unit heats 1 m³ of gas by 1400 K, 1.7 MJ: 30 % of the volatiles' 15 MJ/kg in the cells they burn in) | a campfire: the kindling's tall flame, then the logs' own smaller flames for about 40 s more, leaning in the draught; at 70 s the upper log 13 % wood, 7 % char, 23 % ash, the lower 31 % / 5 % / 19 %, the branch 19 % / 16 % / 18 % (the feedback, not the gas's flame, keeps them burning; with `flame_feedback` 0.15 both logs end at 15 % wood, 24 % ash) |

(The surface feedback alone, without the gas's flames, does not keep a single log burning: in the reference, a log
heated by a 1200 K gas for 40 s chars and goes out with χ up to 0.2 — as a lone log does.)

## Results

Burn Lab on lavapipe (960 × 540, 48 gas cells, 32 voxels per log, `--settle=300`: a capture every 5 s): the
kindling's flame and the crate's corner catching (5 s), the logs' own flames (10-45 s) with embers glowing in the
char's cracks, the logs receding — the upper one collapsing onto its ash — and the fire out at about 50 s, grey heaps
and charred ends left. Every step of the burning is the same on every run (the diagnostics of two runs agree to the
last digit). The RTX job (`m22_f266`: 1080p captures; the cost with the burning drawn and simulated, simulated with
the original meshes, neither) is prepared and waits for the user.

Look issues left: the collapsed shell folds in a saw-tooth where neighbouring vertices fall onto different heights;
a thin plank (a voxel and a half) chars and goes out (the crate).

## Limits

* Skinned meshes are simulated but not drawn burning. A material whose vertex() writes world-space positions
  (`world_vertex_coords`) gets the look, not the deformation.
* At most 8 objects inject into one gas volume (the rest burn without feeding it). Without a gas volume an object is
  heated only by `ignite()` and cools by itself.
* The ash stays inside the object's box (it does not spill onto the ground beyond it); the physics body keeps its
  shape (collision does not follow the burning).
