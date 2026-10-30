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
   it may be an obstacle cell (zero) — or the **surface's own flame**, whichever is hotter: a flame thinner than a
   voxel over the voxels that released volatiles the step before, the voxels beside them (one across: how it creeps
   sideways) and up to `flame_reach` (10 cm) above them (how it climbs), at `flame_temperature` (1100 K), as much as
   their flux per area covers the surface (fully at 5 g/(m² s), wood's critical mass flux for flaming). A burning
   surface exchanges with its flame, not with the cold air beyond it. Semi-implicit (stable at any step).
3. **Pyrolysis** above `pyrolysis_temperature` (573 K; the full rate `pyrolysis_rate` 0.05/s 120 K above): wood →
   char (0.85 of its volume, `char_yield` 0.25 of its mass), endothermic (200 kJ/kg); the volatiles (the other 75 %)
   are released as the gas's fuel (and feed the surface's own flame, 2.).
4. **Char oxidation** above `glow_temperature` (673 K, full 200 K above, `glow_rate` 0.02/s) where the char is
   exposed: char → ash (`ash_volume` 0.15 of its volume — first 0.35, which left heaps of ash nearly a third of the
   wood's volume; wood ash is a few per cent of it, charcoal's ash little more), exothermic (4 MJ per kg of char kept
   by the solid): the embers.
5. **The remains settle**: the loose material — the ash, and the char of a voxel that has crumbled (next to no wood,
   < 0.05, and its char burned three eighths away: 0.6 ash volumes of ash per char left; or next to none left) — falls
   into the voxel below
   (rate 2/s, taking at most 0.6 of its room per step) down to the box's floor; what cannot fall slides through a
   less than half full neighbour into the voxel below that (each of the four at most 0.1 of its room, a quarter of
   what is left): heaps no steeper than a voxel per voxel. The moved material carries its heat (the temperatures
   mixed by heat capacity), so crumbled embers keep glowing in the heap. "Below" is along the voxel axis nearest to
   the world's down (a log modelled upright and laid on its side drops its ash downwards, not along its length).
   (Only ash with no wood or char left falling, straight down, left a burned-out log's thin char and the ash mixed
   in it hanging in the air, invisible, and dropped the rest in two ridges where the log's sides had been; crumbling
   every voxel less than half full instead took the char off the partly covered voxels of thin planks as soon as it
   formed, and the crate burned twice as fast, its flames lying on the ground.) The crumbled debris is porous: no
   obstacle to the gas (under the logs, a heap counted solid shut the gap to the ground and the hot gas streamed out
   along them). The box
   reaches down to the ground:
   at configuration a ray from the object's lowest point down to the physics bodies below it (not its own; at most
   2 m) extends it where the ground is beyond it, and the floor is the layer just above the ground (not the box's
   margin below it, where the ash lay hidden under the ground in the first captures), so a log lying on another
   drops its ash to the ground; without ground the floor is the object's lowest layer.

Empty voxels take the gas's temperature. The heat capacity of a voxel is its materials' (wood 500 kg/m³ × 1700
J/(kg K), char ρ × yield / 0.85, ash 60 × 800), at least 5 % of solid wood's (nearly empty voxels stay stable).
A thin branch (a few voxels thick) heats through and burns away in the time a thick log only chars its skin: the
deformation follows the volume and the density through the heat budget, as the user asked.

## GPU (`fellow_burn_sim.glsl`)

Per object and step (after the gas volumes' steps of the same physics tick, in the tick's order: the objects read the
gas's temperature now, the gas reads their injection at its next step; `process_pending` sorts the volumes' and the
objects' queued steps by tick, then kind, then id): IGNITE (when `ignite()` was called), HEAT (1–4: the state
ping-pong, an emission texture RGBA16F — fuel released, the surface's temperature, its exposure, the solid standing), SETTLE
(5, and the display copy RGBA16F the material samples), INJECT (per gas cell of the region around the object: the
fuel and the surface over a footprint of 2 cells, 64 samples, the solid over the cell, 8), REDUCE every 6 ticks (the
totals, read back: the fractions, `ignited` / `burned_out`). The gas (0117) per object in its region: EMIT heats the
air towards the surface's temperature (`surface_heating` 200/s × the exposure; where fuel is released the
temperature of the voxels releasing it, elsewhere the exposed surface's) — a cell no hotter than 350 K above ambient
(the hot layer on a surface is far thinner than a cell; the gas glows by its temperature from 700 K, and the heat of
the embers' heaps glowed as an orange fog over the ground; the fuel ignites from the surface's own temperature still)
—, adds the fuel (`flame_strength` × 2.7 units
per kg of volatiles, flickering: `flame_flicker` 0.7, the emitters' value noise of a 15 cm period drifting upwards, so
the flames lick instead of standing as smooth sheets) and its smoke (`smoke` 0.05 per unit), and burns
`flame_attachment` (0.5) of it at once where the
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
  distance to the surface then, a reference fill) to where the standing material exceeds half the reference — the
  wood and char, not the ash on them nor the crumbled debris of the heap (as the simulation's SETTLE tells them
  apart): the vertex moves by how far that surface has receded since the start (less a tolerance of 0.15 voxel). A
  vertex that finds none (burned through) is gone, and every triangle touching it is discarded — the ash and the
  debris are the heap's to draw (below). The normal follows the gradient where the surface moved. The composition
  just inside the surface (a quarter voxel: the ash on it greys it) goes to the fragment.
* **fragment()**: the wood browns where it has begun to char, then turns black (`char_color`): charred wood's
  alligator pattern — cellular blocks (`crack_scale` 1.2 cm, 1.8 times as long along the grain, the mesh's longest
  axis), each a little different, raised between narrow cracks (the normal bumped by the height's screen
  derivatives), a faint sheen (roughness 0.6); the char's front ragged (noise on the share it shows at), not along
  the mesh's triangles. As the char burns towards crumbling (its ash per char) it coats with grey ash in patches
  (`ash_color`, matte). Above ~750 K the cracks glow (blackbody colour, `glow_intensity` × (T/1300)³, the
  temperature taken as at most 1300 K: the voxels sit in the flame and take its temperature, glowing charcoal is no
  hotter), the blocks' faces dimly (more at the hottest), some blocks more than others, each breathing slowly (a
  per-block sine of time), softer under the ash. Fewer pixels than cells: the pattern's average (no sparkle).

**The ash heap** (`FellowAshHeap`, an internal child of the mesh instance, made when the first ash appears): a grid
over the box's floor (the ground's layer when the box reaches down to it), two vertices per voxel along each side.
In its own shader each vertex adds up the loose material in the 3 × 3 columns around it from the floor (the voxel
centres) up to the first voxel of standing wood or char (the shell's) or less than a quarter full (the heap's top, or
ash still falling onto it), and rises by that volume over a column's area, smoothed (1 2 1), times a smooth noise
(lumpy, not a pillow); the normal from the heights' slope (Sobel) and the noise's. The heap ends where it is thinner
than a noisy threshold — ragged, curved edges, the threshold's margin interpolated across the triangles and the
fragments beyond it discarded — and meets the floor there. On it, pieces of charcoal: the cells of a cellular
pattern (1.6 crack scales, longer along the burned wood's grain), each shrunk from its border by a random amount, as
many as the char's share allows (at most seven in ten), raised (also in the geometry where the heap is thick
enough), dusted with ash, a few small bits between them; the ash between them grey, mottled, specked with char dust,
matte. Hot, the pieces glow in their cracks, breathing, and the ash dully. The triangles turn with the heap's up
(front faces clockwise from above: the other way round they were lit from below). The voxels are freed only after
the heap's mesh and material (they sample them).

Tried first, from the captures: burned-through vertices moved to the material left drew stretched slivers;
discarding them all left a cage of strips; letting the burned-through and ash-covered vertices fall along the
world's down onto what was left below folded the collapsed shell in a saw-tooth and drew vertical curtains from the
long falls (from the top of a log to the ground); discarding the walls then left thin ribbons; a heap climbing to
where the fill drops below half drew flat slabs with steep sides; the shell marching onto the debris on the ground
drew tents from a log down to it; the heap as a smooth height field with the char's share as its colour looked like
pillows with straight edges; round lumps like a Dalmatian's spots; whole cells of charcoal like a mosaic. A surface
of its own for the loose material, its height its volume, ragged and lumpy, with pieces of charcoal of many sizes,
has none of these.

## Verification (lavapipe)

`burn_check.gd` against `burn_compare.py` (the reference run from the GPU's initial voxels), largest relative L2
difference of T, wood, char, ash:

| case | steps | difference |
|---|---:|---:|
| a log alone (24 voxels along it, no gas), ignited in its middle at 1100 K | 60 | 1.1 × 10⁻⁵ |
| a box on its side (the remains fall along its local x), ignited through, fast; in a static body whose shape is disabled when it has burned away | 120 | 2.2 × 10⁻⁵ (the crumbled char and the ash slid out to the box's edges; the shape disabled) |
| the log over a campfire, one coupled step after 60 (the gas's temperature and the step before's volatiles read back) | 1 | 9.1 × 10⁻⁷ |
| — its emission (half precision) and the gas cells' injection | 1 | ≤ 5.8 × 10⁻⁴ (the solid: the standing wood and char) |

(The reference keeps the step before's volatiles in half precision, as the GPU's emission texture does.)

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
| **2.7 per kg** (1 unit heats 1 m³ of gas by 1400 K, 1.7 MJ: 30 % of the volatiles' 15 MJ/kg in the cells they burn in) | a campfire: the kindling's tall flame, then the logs' own smaller flames for about 40 s more, leaning in the draught; at 70 s the upper log 13 % wood, 7 % char, 23 % ash, the lower 31 % / 5 % / 19 %, the branch 19 % / 16 % / 18 %; the crate (planks a voxel and a half thick) chars at its corner and goes out |
| **the surface's own flame** (2.) instead of the feedback; the crate lit at a bottom corner | **everything burns to ash**: the branch by 15 s, the logs by 45 s (0 % wood, 1.4 % / 1.8 % char, 29 % ash: all of it), the crate's flame climbs its walls and spreads over it in a minute (0.2 % wood, 10 % char, 26 % ash at 70 s) |
| **the remains settling** (5.: crumbled char falls with the ash, slides into heaps, carries its heat) | the wood burns as before, a little faster under the glowing heap (the logs' wood gone by 35-45 s, the branch's by 15 s); the crumbled char lies buried in the heaps' ash and stops glowing when the fire dies: the logs end as **heaps of ash and charcoal** (0 % wood, 11 % / 16 % char, 26 % / 24 % ash at 70 s), the branch 19 % / 23 %; the crate's planks lose their charred skin and burn faster (wood 44 % at 20 s instead of 72 %; 0.2 % wood, 5.6 % char, 28 % ash at 70 s); for a few seconds at 10 s and 25 s the larger fuel release lays the flames along the ground |
| **the look reworked** (2026-10-27: the user found it unrealistic): crumbling by the char burned away (3/8) instead of by the voxel's fill, `ash_volume` 0.15, the debris porous for the gas, the air over hot surfaces no hotter than 350 K above ambient, the fuel flickering; Burn Lab's volume closed but for the top | the flames stand and lick (a campfire's; before, a wind grown across the open volume laid them along the ground, and the embers' heat glowed as an orange fog); the logs' wood gone by 35-40 s, the branch's by 15 s; the fire out at about 30 s, the crate burning on to a minute; the logs end as heaps of charcoal and ash (0 % wood, 13 % / 25 % char, 11 % / 9 % ash at 70 s), the branch 33 % / 8 %, the crate 0.4 % wood, 7 % char, 12 % ash |

The feedback (χ of the volatiles' heat of combustion returned to the voxel releasing them) was not enough for thin
wood: a plank 1.76 voxels thick, lit at a corner without a gas, burned out where it was lit with χ up to 0.3, its
two faces radiating to the cold air (the reference, `study/plank*.py`); with the flame rising over the voxels above it
burned a strip to the top and stopped. The surface's own flame — the surface exchanging with its flame, not with the
air — burns the plank whole from a bottom corner (0.36 × 0.5 m in 350 s of burning time), and a lone log heated for
40 s burns on to ash, as wood that keeps its flame does. A lower `flame_temperature` makes objects that go out by
themselves.

## Results

Burn Lab on lavapipe (480 × 270, 64 gas cells, 32 voxels per log, `--settle=300`: a capture every 5 s): the
kindling's tall flame and the crate's corner catching (5 s), the logs' own flames licking up from them (10-25 s),
embers glowing in the char's cracks and under them in the heap, the logs receding, burning through and crumbling, the
fire out at about 30 s while the crate burns on; at 45-55 s two crossed beds of grey ash with pieces of charcoal in
them where the logs lay, a smaller one for the branch, the crate's remains glowing at their edges. Every step of the
burning is the same on every run (the diagnostics of two runs agree to the last digit). The RTX job (`m22_f266`:
1080p captures; the cost with the burning drawn and simulated, simulated with the original meshes, neither) is
prepared and waits for the user.

## Cost of the drawing (RTX 5070 Ti Laptop, 1080p; milestone 26)

Burn Lab with the burning drawn against the same simulation drawn with the original meshes (`--burn_draw=0`;
`m26_f284`, fellow211, 2 runs, pass timestamps): **+0.84 ms** of GPU time — the opaque pass +0.43, the depth pre-pass
+0.28, the shadows +0.16, the depth's preparation for SSAO +0.06. Two measurement builds told it apart: without the
crack cells (the char's pattern, the heap's pieces) 0.19 ms less (the pre-pass −0.11: its normals for SSAO; the opaque
pass −0.07), without the vertices' march 0.37 ms less (the opaque pass −0.27, the shadows −0.12, the pre-pass −0.05:
the march ran in every pass drawing the mesh). The rest, 0.28 ms, is the subdivided mesh and the fragment's char, ash
and glow.

Two exact changes were tried:

* **The crack cells' neighbours pruned** (`e3c73d3`): a neighbour's point lies inside its cell, so a cell whose box is
  no nearer than the second-nearest point found so far cannot change the pattern — skipped before its hash (the
  comparison with a margin of 10⁻⁵ over the sums' rounding). Burn Lab on lavapipe: 11 images, 0 pixels differ. On
  the RTX no gain (below): the per-neighbour test costs what the skipped hashes save. **Reverted** (`4cb4a82`).
* **The vertices marched once per frame** (`d183ac7`, `fluid/burn_vertices_cached`, on): FellowGas's burn shader
  gets a VERTICES mode doing the material's march (the same expressions) for every vertex of the display after the
  frame's steps — once after configuring, then only in frames with steps (60 per second) — and writes three texels
  per vertex to an RGBA32F texture (1024 vertices per row): the position and whether it is gone, the surface's normal
  and how far the vertex's turns to it (the vertex's own normal is the mesh's compressed one, so the material does the
  mix), the composition at the surface. The materials read them by `VERTEX_ID` plus the surface's first vertex (a
  material per surface, the shader shared). A material whose vertex code may change `VERTEX` or `NORMAL` (an
  assignment, an increment, passed to a function of its own) or that works in world space marches as before.

On the RTX (`m26_f285_burn`, fellow212 = `d183ac7`, Burn Lab at 1080p, 3 runs each, pass timestamps; ms of GPU time):

| Burn Lab | GPU | frame | depth pre-pass | opaque pass | shadows | gas simulation (+ the vertices) |
|---|---:|---:|---:|---:|---:|---:|
| fellow211 (before) | 2.634 | 3.552 | 0.358 | 0.723 | 0.212 | 0.854 |
| the cells pruned, the march in the materials | 2.680 | 3.597 | 0.356 | 0.742 | 0.208 | 0.852 |
| **the cells pruned, the vertices marched once per frame** | **2.139** | **3.084** | 0.230 | 0.455 | 0.074 | 0.882 |

The march once per frame: **−0.50 ms of GPU time (−19 %), the frame −13 %** — the shadows −0.14 (−65 %), the opaque
pass −0.27, the pre-pass −0.13; its compute pass costs 0.03 ms (in the frames with steps). Of the burning's drawing
(+0.84 ms in `m26_f284`) about 0.35 ms is left. With 48 voxels per object (`--burn_res=48`, longer marches;
`m26_f285_burn48`, 2 runs) 2.894 → 2.213 ms (−0.68 ms, −24 %): the opaque pass −0.38, the shadows −0.16, the pre-pass
−0.15, the compute pass +0.03. The images (`m26_f285_burn_capture`, 1080p, the 11 views with and without): 0 to 151
pixels differ per view (504 of 22.8 million), PSNR 81-116 dB — the compute pass rounding differently from the vertex
shader (fused multiply-adds, say), a pixel at an edge here and there; nothing visible.

## Limits

* In a gas volume open at its sides (the default: only the floor closed), Burn Lab's fire grew a wind across the
  volume — several m/s near the ground, 15 m/s at its sides — that laid the flames along the ground (the cold air
  moving too: not the combustion's expansion, whose source is a few hundredths of a cubic metre per second). Burn Lab
  closes its volume but for the top
  (`closed_faces` 55): the flames stand. The open faces' treatment is the gas's (0117); a sponge layer relaxing the
  velocity towards the wind near them would be the fix there.
* Skinned meshes are simulated but not drawn burning. A material whose vertex() writes world-space positions
  (`world_vertex_coords`) gets the look, not the deformation. A material whose vertex() changes `VERTEX` or `NORMAL`
  marches in every pass that draws it (not once per frame).
* At most 8 objects inject into one gas volume (the rest burn without feeding it). Without a gas volume an object
  lit by `ignite()` burns by its own flame, without visible flames.
* The remains stay inside the object's box (the box reaches down to the ground below it at configuration; a heap
  spreads at most to the box's sides — two voxels beyond the mesh — and does not follow the object when it moves).
  The heap of each object is its own (two logs' heaps cross, they do not pile on each other). The physics body keeps its
  shape until less than `collapse_fraction` (0.2) of the wood and char is left; then its shapes are disabled (a rigid
  body frozen first) and `collapsed` is emitted — restored when the node leaves the tree or the burning is reset.
