# 0119 — Gas: sparse bricks (only the air near smoke, heat or fuel simulated)

Status: **KEEP, on by default** (`FellowGasVolume3D.sparse`, milestone 21) · Branch `fellow/geometry` · Settings
`rendering/fellow/fluid/gas_sparse` (measurements: the node's, off, on), `gas_sparse_margin` (0: the node's) ·
Code: `fellow_gas_sim.glsl` (`SPARSE_SOURCE`, `SPARSE_ACTIVATE`, `SPARSE_LIST1`, `sparse_id()`), `fellow_gas.cpp`
(`_run_step`, `_dispatch_sim`'s lists); reference `modules/fellow_fluid/tests/gas_reference.py` (`sparse_*`).

## Problem

Milestone 20's solver (0117) runs every pass over every cell of the box, but the smoke of a campfire or a chimney
fills a third of it or less: the rest is still air whose simulation changes nothing visible. On the RTX Fire Lab's
three volumes cost 1.7 ms per step at `--gas_res=64` (0.47 M cells) and 5.5 ms at 128 (3.2 M cells). Where that time
went (`m20_f262`, a timestamp before each group of passes, the three volumes' sum):

| `--gas_res` | emission | advection | forces | pressure | projection, bricks |
|---|---:|---:|---:|---:|---:|
| 64 | 0.05 | 0.12 | 0.07 | **1.96 ms** | 0.34 |
| 128 | 0.15 | 0.84 | 0.39 | **3.96 ms** | 0.71 |

## Change

The box is divided into bricks of 4³ cells (the velocity's texels t in brick t / 4: one more layer where a side is a
multiple of 4). Per step, before the other passes:

* **SPARSE_SOURCE**: a brick is a source where its largest smoke, temperature or fuel (COARSE's maxima of the last
  step, now RGBA16F with the fuel; over the brick and a one-cell border) is above 10⁻³, 1 K, 10⁻³, or where an
  emitter's bounding sphere and its one-cell ramp reach it.
* **SPARSE_ACTIVATE**: **A** = the bricks within `sparse_margin` (2) bricks of a source (Chebyshev distance), whose
  cells are simulated; **F** = within one brick more, whose velocity is (A's faces and the cells around it, so the
  velocity next to A is advected, forced and projected). A brick that leaves F gets the ambient velocity (the wind)
  and no solid, zero hats; one that leaves A no smoke and zero pressure (the divergence outside A is never read).
  Both are listed (atomics) for indirect dispatches. The sources around a workgroup's bricks come from shared
  memory.
* The passes run over the lists, one workgroup (4 × 4 × 4) per brick: emission, scalar advection (MacCormack's hat
  over F, the correction over A), divergence, the grid's pressure sweeps and the prolongation into it over A;
  obstacles, velocity advection, curl, forces, projection over F. COARSE stays dense with an early exit outside A
  (the fire light's per-workgroup sums keep their order: the same light on every run).
* **The pressure outside A is zero** (Dirichlet: the air beyond is still): the cells outside keep p = 0 and the
  stencil sees them as neighbours with zero pressure. On the coarser levels a cell is a **zero cell** where any of
  its children is outside A or a zero cell (solid texture 255): the condition is kept on every level. (With "all
  children" the zero cells disappeared from the coarser levels of a closed box, those levels became the closed box's
  singular problem with an incompatible right-hand side, and the V-cycle diverged: the pressure 480, the velocity at
  its clamp within 10 steps.) **SPARSE_LIST1** lists the first coarser level's bricks that have a cell of A below;
  its sweeps and the prolongation into it run over them (MG_RESTRICT still writes the whole level, the unlisted cells
  zero cells).

A world-space volume that moved recomputes the maxima from the shifted state (all bricks) and treats every brick as
simulated before: what is not in A or F now is cleared, by SPARSE_CLEAR (one invocation per texel; Fire Lab's torch
shifts almost every step at 128 cells), as after dense steps.

## Verification

The reference models it: the masks from the same maxima (in half precision), the passes' results kept where the GPU
writes them, the ambient values elsewhere, the zero cells. Lavapipe, `gas_check.gd --sparse=1` against
`gas_compare.py`, largest relative L2 difference of any field:

| case | steps | sparse (margin 2) |
|---|---:|---:|
| campfire | 10, 60 | 3.5 × 10⁻⁷, 2.5 × 10⁻⁶ |
| a static sphere obstacle | 30 | 9.1 × 10⁻⁷ |
| wind through the x faces | 30 | 8.0 × 10⁻⁷ |
| closed box, warm smoke | 30 | 7.5 × 10⁻⁷ |
| world space, moved after 20 steps | 30 | 8.2 × 10⁻⁷ |
| campfire, SOR | 60 | 3.1 × 10⁻⁶ |
| campfire, margin 1 | 20, 40, 60 | 6.8 × 10⁻⁷, 4.2 × 10⁻⁴, 8.8 × 10⁻⁴ (a brick crossing a threshold in one and not the other) |

Two sparse runs: the same state bit for bit. Dense volumes (`sparse` off): bit for bit the previous build's. The
reference's campfire (32 × 64 × 32) after 60 steps against dense: A 37 % of the bricks, F 53 %, the smoke's relative
L2 difference 1.9 % (margin 1: A 24 %, F 43 %, 9.8 %); the residual after the projection the same (0.0012 s⁻¹).
Fire Lab, sparse against dense (lavapipe and RTX captures): the same fire, smoke and plume; the chaotic flow's
details differ as between any two solvers.

## Results

RTX 5070 Ti Laptop, Fire Lab's three volumes at 1080p, in one process (blocks of 30 frames switching the setting,
120 block pairs; GPU timestamps):

| `--gas_res` | dense: simulation / frame | **sparse**: simulation / frame | sparse with SOR: simulation |
|---|---:|---:|---:|
| 64 (0.47 M cells) | 1.77 / 3.38 ms | **1.58 / 3.10 ms** (−11 % / −8.2 %) | 0.84 ms |
| 128 (3.24 M cells) | 6.43 / 8.52 ms | **3.16 / 5.04 ms** (−51 % / −41 %) | 2.88 ms |

(`m21_f265`, `fellow198`. The first version, `m20_f263`: −5.5 % and −41 % of the simulation; then the activation's
sources from shared memory and the first coarser level over its bricks, `m21_f264`: −4 % and −46 %; then the parallel
clear after a shift, `m21_f265`. The dense numbers vary between sessions by up to 15 %: the reference is each run's.)

Where the step's time goes at 128 cells (a timestamp before each group of passes, which serialises the volumes):

| group | dense | sparse |
|---|---:|---:|
| emission (sparse: the sources, A and F, the lists, the clears) | 0.16 | 0.34 |
| advection | 0.84 | 0.25 |
| forces | 0.54 | 0.17 |
| pressure | 4.49 | 2.62 |
| projection, bricks | 0.72 | 0.29 |

At 64 cells the passes are small enough that their dispatches waiting for each other dominate (the sparse ones add
three per volume): the pressure 2.00 → 1.85 ms, the emission group 0.04 → 0.18. The multigrid solve's coarser levels
(dense except the first) are most of what remains; SOR, with fewer and larger dispatches, is cheaper there (and its
residual about 10× higher, 0117).

## Also measured: the multigrid's small levels in one workgroup (off)

`gas_mg_tail_cells` (0): the levels from the first with at most that many cells down to the coarsest and back in one
workgroup of 256 (MG_TAIL: the per-level passes' operations in the same order, a barrier after each; the coarser
levels in one atlas texture per field, addressed by an x offset). The same state bit for bit (lavapipe). On the RTX
(`m20_f262`) it does not pay: at 64 cells the frame −2.0 % with 2048, +21 % with 8192, +66 % with 32768; at 128 cells
+7.8 %, +21 %, +38 % — one workgroup works through a level of a few thousand cells more slowly than the dispatches
it replaces wait for each other. Kept as an option (0).
