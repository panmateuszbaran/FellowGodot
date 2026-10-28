# FellowEngine — milestone 22 report (burning objects: char, embers, ash)

Status: **measured** (lavapipe; RTX in job `m23_f267`, 2026-10-28, `fellow200` — milestone 23's build, the burning
unchanged). Branch `fellow/geometry`. Decision record `docs/decisions/0120`; user-facing summary
`docs/FELLOWENGINE_OPTIONS.md` ("Burning objects").

## 0. Result

The user asked (2026-10-26) for a burning mechanism in the engine: objects deforming in the fire by their volume and
density, charring, and ash left after the burning. A new node, **`FellowBurnable3D`**, makes a `MeshInstance3D` burn
like wood in a gas volume's fire (milestones 20-21):

* **Simulation** (GPU, per object a box of voxels: temperature, wood, char, ash): conduction, the heat of the gas's
  flame or the surface's own (sub-voxel) flame, which climbs and creeps over the wood, pyrolysis (wood → char; the
  volatiles are the gas's fuel — the flames come from the object), char oxidation (embers → ash), the remains (the
  ash, and the char where it has crumbled) falling along the world's down to the ground and sliding into heaps no
  steeper than a voxel per voxel, carrying their heat (crumbled embers keep glowing). Thin parts heat through and
  burn away first, thick ones char slowly.
* **Coupling** both ways with `FellowGasVolume3D`: the gas reads each object's fuel, smoke, hot surface and solid
  cells (it flows around the object's voxels, not its physics body); the object reads the gas's temperature.
* **Drawing** on the object's own material (BaseMaterial3D or ShaderMaterial, extended): the mesh subdivided, its
  vertices receding with the wood and char, char and ash colours, embers glowing in cracks; burned-through parts are
  not drawn, the ash and the crumbled charcoal are a heap of its own (a grid over the floor, each vertex risen by the
  loose material in its voxel column).
* **Verification**: the GPU matches a NumPy reference (`burn_reference.py`) to ~10⁻⁵ (relative L2 after 60-120
  steps), the coupling's injection to half-precision rounding; Fire Lab and the gas checks unchanged bit for bit (the burning objects' terms in the gas are theirs alone).
* **Burn Lab** (`--scene=burn`): two logs and a branch over a kindling fire (out after 12 s), a crate lit at a
  bottom corner. **The wood burns away on its own** — the branch by 15 s, the logs by 35-40 s, the crate's planks by
  about a minute — with flames that stand and lick, and **the remains crumble into beds of grey ash with pieces of
  charcoal on the ground**, glowing while hot (the logs: 0 % wood, 13-25 % char, 9-11 % ash of their volume at 70 s;
  the crate 0.4 % / 7 % / 12 %).
* **Collision**: once less than a fifth of the wood and char is left, the object's physics body stops colliding
  (`collapse_fraction`, signal `collapsed`).

## 1. Development

* **The model first** (`burn_reference.py`): per voxel conduction (harmonic face conductivity), convection and
  radiation with the gas (semi-implicit), pyrolysis, char oxidation, ash settling; a study with its own flame model
  burned a log to ash and cooled it.
* **GPU** (`fellow_burn_sim.glsl`, `fellow_gas_burn.cpp`): IGNITE, HEAT, SETTLE, INJECT (the gas cells around the
  object), REDUCE (totals read back every 6 ticks: fractions, `ignited`, `burned_out`). The gas's EMIT, OBSTACLES and
  sparse bricks read the objects' injection textures (up to 8 per volume). The volumes' and the objects' steps run
  in the order of their physics ticks (an object reads the gas of its tick).
* **The node**: voxelization (rays through 2 × 2 points per voxel column along each axis, inside intervals by parity,
  the median of the three axes: an open mesh spoils one), display mesh by conforming longest-edge bisection (edges
  marked by position: no cracks at seams), the material's shader code extended (vertex march, char, cracks), the
  physics body excluded from the gas's obstacles.
* **Calibration** (0120's table, `burn_diag.gd`): the gas first ran away (8 200 K above ambient), then the fire went
  out with the kindling at every fuel rate; the fixes, in order: a flame temperature limit for the gas, the fuel's
  temperature taken from the voxels releasing it, faster surface heating, part of the fuel burning at the surface,
  the surface flame's heat fed back into the voxel, and the fuel per kg lowered until the flames were a campfire's
  (with more fuel the combustion's expansion blew them along the ground). The logs then burned two thirds away, but
  thin wood (the crate's planks, 1.76 voxels) went out: its two faces radiated to the cold air. The feedback was
  replaced by **the surface's own flame** — a burning surface exchanges heat with its flame (over the voxels releasing
  volatiles, beside them, up to 10 cm above), not with the air beyond — and everything burns to ash.
* **Drawing fixes from the captures**: burned-through vertices moved to the material left drew slivers, discarding
  their triangles left a cage of strips, letting them fall onto what was left below folded the collapsed shell in a
  saw-tooth and drew curtains from the long falls — the ash got its own mesh, **the ash heap** (`FellowAshHeap`), and
  the shell is drawn only where wood or char is left; the cracks smaller (2.5 cm), redder, anti-aliased, the glow
  capped at charcoal's temperature. The box reaches down to the ground below the object (a ray at configuration),
  so the ash of a log lying on another falls to the ground (first into the box's margin under the ground, hidden:
  the floor is now the ground's layer, a voxel face aligned with it).
* **The remains** (from a voxel dump of a burned-out log): only ash with no char left fell, so a log's thin leftover
  char and the ash mixed in it hung in the air, invisible, and the rest dropped in two ridges where its sides had
  been. Now the char of a crumbled voxel (no wood, less than half full, or debris with ash in it) falls with the
  ash, the loose material slides down beside where it cannot fall (a 45° heap), and it carries its heat; the shell
  draws only standing wood and char, the heap the rest. The reference got the same rule (`ash` case 2.2 × 10⁻⁵, the
  heap reaching the box's sides). The heap's triangles were wound upside down for one floor orientation (lit from
  below); fixed. A teardown error ("Texture ... is not a valid texture") came from the heap's material outliving the
  voxels it samples: they are freed last now.

* **The look reworked** (2026-10-27, the user: "it seems not very realistic"), from captures and a voxel/gas dump:
  * the flames lay on the ground: in Burn Lab's gas volume, open at its sides, the fire grew a wind across it
    (several m/s by the ground, 15 m/s at its sides; the cold air moving, not the expansion) — Burn Lab closes its
    volume but for the top, and the flames stand; the fuel released now flickers (`flame_flicker` 0.7: the emitters'
    value noise), so they lick; the air over hot surfaces is no hotter than 350 K above ambient (the embers' heat had
    glowed as an orange fog); the crumbled debris is no obstacle to the gas (porous);
  * crumbling by the char burned away (three eighths) instead of by the voxel's fill: thin planks lost their char as
    soon as it formed and burned twice as fast; `ash_volume` 0.35 → 0.15 (real wood ash is a few per cent);
  * the char: alligator cracks along the grain (1.2 cm, 1.8 times as long along the mesh's longest axis), raised
    blocks, a faint sheen, a ragged front, grey ash coating it in patches before it crumbles, the embers breathing in
    the cracks, the faces glowing dimly;
  * the heap: ragged curved edges, lumpy, pieces of charcoal of many sizes (longer along the grain) raised and dusted
    in mottled grey ash, glowing in their cracks (tried: smooth pillows, round lumps like a Dalmatian's spots, a mosaic
    of whole cells).

## 2. Verification (lavapipe)

| check | result |
|---|---|
| `burn_check.gd --case=solo` (a log alone, ignited), 60 steps | 1.1 × 10⁻⁵ (T, wood, char, ash; relative L2) |
| `--case=ash` (a box on its side in a static body, burned through fast, the remains falling along its local x and sliding into a heap), 120 steps | 2.2 × 10⁻⁵; the body's shape disabled |
| `--case=gas` (over a campfire), one coupled step after 60 | 9.1 × 10⁻⁷; emission and injection ≤ 5.8 × 10⁻⁴ (half precision) |
| `gas_check.gd` campfire dense / sparse, 60 steps | the same state bit for bit as milestone 21's |
| `gas_check.gd --flame_limit=1500` against `gas_compare.py` | 3.1 × 10⁻⁶ |
| Fire Lab captures (fire_close, fire_chimney, fire_torch) against `fellow198`'s code | 0 differing pixels |

## 3. Burn Lab (lavapipe captures, 480 × 270, 64 gas cells)

Every 5 s: the kindling's tall flame and the crate's corner catching (5 s), the logs' own flames licking up from them
(10-25 s), embers glowing in the char's cracks and in the heap under them, the logs receding, burning through and
crumbling, the fire out at about 30 s while the crate burns on, crossed beds of ash and charcoal left where the logs
lay (45-55 s). The same burning on every run (two runs' fractions agree to the last digit). Files:
`m24_burn_progress.jpg` (5-55 s), `m24_flames_before_after.jpg` (the volume's sides open and closed),
`m24_char_heaps.jpg` (the char and the heaps in daylight: two crossed logs and a box burned without gas).

## 4. RTX 5070 Ti Laptop (`m22_f267_burn_*`, fellow200, 1080p, autotune off)

Job `m22_f266` was never run (the laptop was to stay idle); its measurements ran in `m23_f267` with `fellow200`.

* **Captures** (`m22_f267_burn_capture`: 11 cameras over 55 s): the scene burns on the RTX as on lavapipe — standing
  flames over the logs and the crate, charred and cracked wood, the remains as ash beds with charcoal pieces. The
  beds' contrast is high at 1080p (dark charcoal pieces on light ash, a spotted look): left for the appearance work.
* **Cost per frame** (`m22_f267_burn_cost`, 2 runs each, the burn steps timestamped):

| | frame | GPU | render thread | opaque pass |
|---|---:|---:|---:|---:|
| burning drawn and simulated | 3.417 ms | 2.548 ms | 0.560 ms | 0.713 ms |
| simulated, original meshes drawn | 2.594 ms | 1.742 ms | 0.512 ms | 0.266 ms |
| neither (the gas's fire only) | 2.387 ms | 1.710 ms | 0.485 ms | 0.283 ms |

The simulation of four objects costs about 0.2 ms of the frame (0.03 ms of GPU time: the objects' steps are small next
to the gas's); drawing them burning costs 0.8 ms — the subdivided meshes reading the voxels in every pass (opaque +0.45
ms, depth pre-pass and shadows the rest).

## 5. Open

* The physics body stops colliding at once (no gradual shrinking); the remains stay in the object's box (reaching
  down to the ground found at configuration; a heap spreads at most to its sides, does not follow a moved object).
* The crumbled char buried in a heap stops glowing when the fire dies: the logs end with 13-25 % of their volume as
  charcoal, not all ash (earlier, char left standing burned to 1.4-1.8 %, but floated invisibly when thin).
* A gas volume open at its sides can grow a wind across it under a large fire (Burn Lab closes its sides); a sponge
  layer at the open faces would be the gas's fix (0117), not done (it changes Fire Lab).
* The scene's own art is plain (untextured logs, spheres for stones, a flat ground): the burning is drawn on whatever
  material the mesh has.
* Skinned meshes are simulated, not drawn burning; at most 8 objects per gas volume.
