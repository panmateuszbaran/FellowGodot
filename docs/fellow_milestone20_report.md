# FellowEngine — milestone 20 report (smoke and fire)

Status: **FINAL** (`fellow193`-`fellow194`; final measurement `m20_f260_final`, the gas volumes' cost `m20_f261`,
2026-10-26). Branch `fellow/geometry` (`dddc9a9`). Benchmark machine as before (Acer Predator PHN16S-71, Core Ultra 9
275HX, RTX 5070 Ti Laptop GPU, NVIDIA GPU only, Windows 11, 1920×1080, "high" preset). Decision records
`docs/decisions/0117`-`0118`; user-facing summary `docs/FELLOWENGINE_OPTIONS.md` ("Gas volumes").

## 0. Result

The user asked for a built-in simulator of fluids, smoke and fire (2026-10-25). This milestone adds the gas part —
smoke and fire — as the first stage of the plan (liquids next, on the same grid and solver):

* **`FellowGasVolume3D`** simulates a box of air on the GPU, one step per physics tick: a staggered grid, MacCormack
  advection, combustion (fuel burns above an ignition temperature into heat, soot and expansion), buoyancy, vorticity
  confinement, turbulence, wind, a multigrid pressure projection; the physics bodies inside it are moving obstacles
  (one shape query per tick). Its **`FellowGasEmitter3D`** children emit smoke, fuel and heat (0117).
* The **Forward+ renderer** draws it: a half-resolution ray march with the smoke's self-shadow (Wrenninge's
  multiple-scattering octaves), the sky's light, the fire's blackbody emission, the smoke's shadow on the scene (in the
  contact shadow mask), composited by depth before the transparent pass; an **omni light** at the fire, placed and
  coloured from the simulated emission (read back asynchronously); the velocity as a texture for **GPU particles**
  (embers) (0118).
* **Verified**: the GPU's state equals a NumPy port of the solver to float rounding (largest relative L2 difference
  after 30-60 steps ≤ 2.5 × 10⁻⁶ in every case — a campfire, an obstacle, wind, a closed box, a moving world-space box,
  an odd grid — except the campfire's fuel after 60 steps, 9.6 × 10⁻⁵, where one cell's gas crossed the ignition
  temperature in one and not in the other); two runs of Fire Lab render the same frames bit for bit; without a gas
  volume every frame is milestone 19's bit for bit (City Lab, Crowd Lab, Lights Lab, City Lab with auto exposure and
  the dirt map: 0 differing pixels).
* **Cost on the RTX** (Fire Lab's three volumes at 1080p, `m20_f261`): at `--gas_res=64` (0.47 M cells) the simulation
  1.69 ms of GPU per step, the drawing 0.31 ms, the frame 3.25 ms (1.76 without the simulation); at 128 (3.24 M cells)
  5.62 and 0.45 ms, the frame 7.52 ms (section 3).
* **The defaults are not slower**: stock against `fellow193` in the nine scenes, three runs each (`m20_f260_final`),
  FellowEngine faster in every scene and every pair of runs, −8.2 % (Stress Lab) to −53.7 % (Sponza) (section 4).

## 1. Development

* **The solver first in NumPy** (`modules/fellow_fluid/tests/gas_reference.py`): the GPU passes one by one, then the
  pressure solve's choice — SOR (ω, iterations, warm start) and a multigrid V-cycle (prolongation, smoother weight) on
  the campfire's state, the residual after one projection as the measure (0117's table).
* **The fire's colour.** A blackbody at the gas's temperature made the flames pink-red under AgX (1500 K: red
  channel three times the luminance, no blue); the emission's colour now comes from the temperature × 1.4
  (`fire_temperature_scale`): orange to yellow flames, red tips, the fire light orange.
* **Teleported bodies.** In the bench's temporal mode Fire Lab's sphere (a kinematic body whose position the scene
  sets) jumped once from where the build left it; the physics server reported the jump as 100 m/s for one tick, the
  gas took that push, and SOR's warm start kept its pressure for seconds: the campfire's flames lay flat along the
  ground for the whole sequence. A body faster than six cells per step now counts as teleported, and the multigrid
  solve (two V-cycles, 9-17 × lower residual than 20 SOR sweeps for less work) recovers from such a transient within a
  step or two.
* **The chimney's NaN** (found by f259 on the RTX: the chimney's plume missing from every capture). The multigrid
  solve counted an open face with weight 1 on every level; the chimney's grid (53 × 80 × 53, every face open) is odd,
  its coarser levels hang over the faces, and the V-cycle diverged: the pressure NaN within seconds (lavapipe and the
  reference too; closed, or with SOR, finite). A coarse level now counts an open face with 1 / its distance to the
  grid's zero pressure: every grid size tried converges, 10× per V-cycle on most (0117, "Open faces on the coarse
  levels"); the chimney's state finite after 120 steps on lavapipe (`gas_diag.gd`), SOR unchanged bit for bit, the
  GPU the reference to float rounding (32 × 48 × 32 and 34 × 51 × 34).
* **re-spirv**, Godot's SPIR-V optimizer, rejected the solver's dynamic vector indexing (`v[c]` with a run-time `c`;
  "OpVectorExtractDynamic is not supported yet", and the shader unoptimized): replaced by selects.

## 2. Fire Lab

A new bench scene (`--scene=fire`): a campfire (2.4 × 4.8 × 2.4 m, its logs static bodies the flames flow around, the
fire light, 64 embers), a house whose chimney smokes into a light wind (6 × 9 × 6 m), a sphere circling through the
campfire's smoke (an animatable body), a torch (a world-space volume, 3 × 3 × 3 m, carried around in the temporal
sequence `fire_torch_walk`); `--gas_res` (64: the campfire 32 × 64 × 32 cells, the chimney 53 × 80 × 53, the torch
48³), `--night=1`, `--gas_render=0`, `--gas_sim=0`, `--gas_prewarm=0`, `--gas_chimney=0`, `--gas_sphere=0`,
`--gas_embers=0`, `--gas_torch=0`. Cameras `fire_close`, `fire_overview`, `fire_chimney`, `fire_torch`; sequences
`fire_orbit`, `fire_torch_walk`.

Captures: lavapipe (960 × 540, `m20_fire_lab_lavapipe.jpg`); the RTX at 1080p (`m20_f261_capture`, `_night`,
`_temporal`; sent to the user as `m20_rtx_day.jpg`, `m20_rtx_night.jpg`, `m20_rtx_temporal.jpg`): the flames orange to
yellow between the logs, the embers, the smoke's shadow on the ground, the sphere's shadow through the smoke, the
chimney's white plume bent by the wind and its shadow beside the house, the torch's flame trailing as it is carried;
at night the campfire and the torch the only lights, the stones and the ground lit orange.

## 3. The gas volumes' cost (RTX)

`m20_f261` (`fellow194`, autotune off, 1080p, in one process: blocks of 30 frames switching the settings, 120 block
pairs; GPU timestamps):

| | `--gas_res=64` (0.47 M cells) | `--gas_res=128` (3.24 M cells) |
|---|---:|---:|
| frame, everything on (multigrid) | 3.25 ms | 7.52 ms |
| the SOR solve instead | 2.78 ms (−14.4 %) | 8.05 ms (+7.1 %) |
| without the simulation (the last state drawn) | 1.76 ms (−45.7 %) | 1.87 ms (−75.1 %) |
| without the drawing | 3.27 ms (+0.5 %, n.s.) | 7.05 ms (−6.2 %) |
| GPU: simulation, multigrid / SOR | 1.69 / 1.08 ms | 5.62 / 6.03 ms |
| GPU: Light, Shadow, Trace, Composite | 0.048, 0.043, 0.191, 0.025 ms | 0.149, 0.048, 0.221, 0.030 ms |
| CPU: recording the steps | 0.15 ms | 0.20 ms |

At 128 cells the multigrid solve is cheaper than SOR's 20 sweeps; at 64 its many small dispatches on the coarse
levels wait on each other and it costs 0.2 ms per volume more, for a residual 10× lower (0117). The drawing is small
next to the simulation; the simulation is what a project sizes (cells × volumes: 1.7 ns per cell and step at 128).

The chimney alone after 120 steps (`gas_diag.gd` on the RTX): finite at 53 × 80 × 53 and 107 × 160 × 107 (smoke total
429 and 2270). `m20_f259` (`fellow193`, before the fix) measured the same set: its multigrid numbers are void (the
chimney's NaN), its SOR ones agree (simulation 0.91 and 5.89 ms).

## 4. Final measurement: stock against the defaults

`m20_f260_final` (stock Godot 4.7.2 against `fellow193` with its defaults, 3 runs per build and scene after a pass that
filled the shader caches, 20 s each after a 30 s heat soak, 1080p "high"; `fellow194` differs from it only in the gas
solve, which these scenes do not run). Milestone 14's final (`m14_f213_final`) for comparison; between sessions the
machine's speed varies by several per cent (stock 1-14 % faster today), so the reference is each session's stock:

| scene | stock (ms) | **fellow193** | against stock | pairs faster | m14 against its stock |
|---|---:|---:|---:|---:|---:|
| Sponza | 13.82 | **6.40** | **−53.7 %** | 3/3 | −52.3 % |
| Bistro exterior | 6.33 | **4.44** | **−29.8 %** | 3/3 (stock noisy, CV 5.6 %) | −36.0 % |
| City Lab | 2.88 | **2.10** | **−27.0 %** | 3/3 (stock noisy, CV 4.6 %) | −33.3 % |
| Bistro interior | 4.31 | **3.33** | **−22.7 %** | 3/3 | −21.8 % |
| Lights Lab | 3.05 | **2.36** | **−22.7 %** | 3/3 | −24.3 % |
| Crowd Lab | 5.63 | **4.49** | **−20.2 %** | 3/3 | (new in milestone 15: −12.3 %) |
| Foliage Lab | 3.41 | **2.86** | **−16.2 %** | 3/3 | −17.9 % |
| PBR Lab | 2.47 | **2.09** | **−15.5 %** | 3/3 | −18.0 % |
| Stress Lab | 4.82 | **4.43** | **−8.2 %** | 3/3 | −5.4 % |

Milestones 15-20 kept the defaults' speed: FellowEngine's own times are equal to or lower than milestone 14's in
every scene (Sponza 6.66 → 6.40 ms, Bistro exterior 4.52 → 4.44, City Lab 2.24 → 2.10, Stress Lab 5.00 → 4.43); the
ratios that are lower (Bistro exterior, City Lab) are stock's faster and noisier runs in this session.

## 5. Open

* **The multigrid solve on small grids**: about 80 dispatches per volume and step, most of them on coarse levels of a
  few hundred cells, latency-bound. The levels below a few thousand cells in one workgroup (as the coarsest already is)
  would make it cheaper than SOR there too.
* **The simulation's cost at high resolution** (5.6 ms for 3.2 M cells): sparse bricks (only the bricks with smoke or
  heat and their neighbours simulated) and a domain that follows its emitter are the plan's next step for large fires.
* Transparent objects behind the smoke are drawn over it; the smoke is not in the volumetric fog.
* No editor gizmo for the box yet (the editor was not built in this milestone).
* Next stages of the plan: liquids (FLIP / APIC on this grid and solver, screen-space surface), two-way coupling with
  Jolt.

## 6. Jobs

* `m20_f259` (`fellow193` from `f729cd5`): Fire Lab's captures and the gas A/B at 64 and 128 cells (found the
  chimney's NaN).
* `m20_f260` (`fellow193`): the final measurement against stock (section 4).
* `m20_f261` (`fellow194` from `dddc9a9`): the chimney's state on the RTX, the captures and the A/B again (section 3).
