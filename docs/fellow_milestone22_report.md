# FellowEngine — milestone 22 report (burning objects: char, embers, ash)

Status: **DRAFT — lavapipe only** (the RTX job `m22_f266` is prepared, not run: the user asked to leave the laptop
idle, 2026-10-26). Branch `fellow/geometry`. Decision record `docs/decisions/0120`; user-facing summary
`docs/FELLOWENGINE_OPTIONS.md` ("Burning objects").

## 0. Result

The user asked (2026-10-26) for a burning mechanism in the engine: objects deforming in the fire by their volume and
density, charring, and ash left after the burning. A new node, **`FellowBurnable3D`**, makes a `MeshInstance3D` burn
like wood in a gas volume's fire (milestones 20-21):

* **Simulation** (GPU, per object a box of voxels: temperature, wood, char, ash): conduction, the heat of the gas's
  flame, pyrolysis (wood → char; the volatiles are the gas's fuel — the flames come from the object), the surface
  flame's heat fed back, char oxidation (embers → ash), ash falling along the world's down into a heap. Thin parts
  heat through and burn away first, thick ones char slowly.
* **Coupling** both ways with `FellowGasVolume3D`: the gas reads each object's fuel, smoke, hot surface and solid
  cells (it flows around the object's voxels, not its physics body); the object reads the gas's temperature.
* **Drawing** on the object's own material (BaseMaterial3D or ShaderMaterial, extended): the mesh subdivided, its
  vertices receding with the material, char and ash colours, embers glowing in cracks, burned-through parts falling
  onto the remains.
* **Verification**: the GPU matches a NumPy reference (`burn_reference.py`) to ~10⁻⁵ (relative L2 after 60-120
  steps), the coupling's injection to half-precision rounding; Fire Lab and the gas checks unchanged bit for bit.
* **Burn Lab** (`--scene=burn`): two logs and a branch over a kindling fire (out after 12 s), a crate lit at a
  corner. The logs burn on their own after the kindling for about 40 s: at 70 s the upper log is 13 % wood, 7 % char,
  23 % ash (of its initial volume; 30 % is all of it as ash), the lower 31 % / 5 % / 19 %, the branch 19 % / 16 % /
  18 %.

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
  the surface flame's heat fed back into the voxel (what keeps the object burning), and the fuel per kg lowered until
  the flames were a campfire's (with more fuel the combustion's expansion blew them along the ground).
* **Drawing fixes from the captures**: burned-through vertices moved to the material left drew slivers, discarding
  their triangles left a cage of strips — they now fall onto what is left below; the cracks smaller (2.5 cm), redder,
  anti-aliased, the glow capped at charcoal's temperature.

## 2. Verification (lavapipe)

| check | result |
|---|---|
| `burn_check.gd --case=solo` (a log alone, ignited), 60 steps | 1.1 × 10⁻⁵ (T, wood, char, ash; relative L2) |
| `--case=ash` (a box on its side, burned through fast, the ash falling along its local x), 120 steps | 1.7 × 10⁻⁶ |
| `--case=gas` (over a campfire), one coupled step after 60 | 9.2 × 10⁻⁷; emission and injection ≤ 5.6 × 10⁻⁴ (half precision) |
| `gas_check.gd` campfire dense / sparse, 60 steps | the same state bit for bit as milestone 21's |
| `gas_check.gd --flame_limit=1500` against `gas_compare.py` | 3.1 × 10⁻⁶ |
| Fire Lab captures (fire_close, fire_chimney, fire_torch) against `fellow198`'s code | 0 differing pixels |

## 3. Burn Lab (lavapipe captures, 960 × 540, 48 gas cells)

Every 5 s: the kindling's tall flame and the crate's corner catching (5 s), the logs' own flames (10-20 s), embers
glowing in the char's cracks, the logs receding and collapsing onto their ash (25-50 s), the fire out at about 50 s.
The same burning on every run (two runs' fractions agree to the last digit). Files: `m22_burn_progress.jpg`,
`m22_burn_top_55s.jpg`.

## 4. RTX (not run)

Job `m22_f266` (prepared, `fellow199`): Burn Lab captures at 1080p over 55 s; the cost per frame with the burning
drawn and simulated, simulated with the original meshes, neither (the burn steps' own time from a timestamp at the
switch between the gas's and the objects' steps). Queued only when the user agrees.

## 5. Open

* The objects' physics bodies keep their shape (collision does not follow the burning); the ash stays in the
  object's box.
* Skinned meshes are simulated, not drawn burning; at most 8 objects per gas volume.
* The crate (planks a voxel and a half thick) chars at its corner and goes out: thin planks lose heat faster than
  their small flames return.
* The collapsed shell folds in a saw-tooth where neighbouring vertices fall onto different heights.
* `flame_feedback` 0.15 burns Burn Lab's logs more completely (both to 15 % wood, 24 % ash at 70 s) than the default
  0.1.
