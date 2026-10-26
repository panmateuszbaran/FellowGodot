# FellowEngine — milestone 21 report (gas: where the time goes, sparse bricks)

Status: **FINAL** (`fellow195`-`fellow198`, measured 2026-10-26). Branch `fellow/geometry` (`95d87f3`). Benchmark
machine as before (Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop GPU, NVIDIA GPU only, Windows 11,
1920×1080). Decision record `docs/decisions/0119`; user-facing summary `docs/FELLOWENGINE_OPTIONS.md` ("Gas
volumes").

## 0. Result

Milestone 20 left two directions for the gas volumes' cost (its report, section 5); the user asked to work on them
(2026-10-26). Fire Lab's three volumes (a campfire, a chimney, a carried torch), simulation per step on the RTX,
in one process against the dense solver:

| `--gas_res` | dense | **sparse bricks** (the default now) | frame |
|---|---:|---:|---:|
| 64 (0.47 M cells) | 1.77 ms | **1.58 ms (−11 %)** | 3.38 → 3.10 ms (−8.2 %) |
| 128 (3.24 M cells) | 6.43 ms | **3.16 ms (−51 %)** | 8.52 → 5.04 ms (−41 %) |

* **Sparse bricks** (0119, on by default): only the 4³ bricks within two bricks of smoke, heat, fuel or an emitter
  are simulated (their velocity one brick further), the air beyond still. The GPU matches the NumPy reference, which
  models it, to float rounding; two runs are the same bit for bit; dense volumes are milestone 20's bit for bit.
* **Where a step's time goes**: the pressure solve is 65-77 % of it (`m20_f262`); sparse bricks take the most off
  the passes over the grid, the multigrid's coarser levels remain.
* **Measured and left off**: the multigrid's small levels in one workgroup (slower: one workgroup works through a
  level of a few thousand cells more slowly than the dispatches it replaces wait for each other).

## 1. Development

* **The profile first** (`m20_f262`, `rendering/fellow/fluid/gas_profile_passes`: a timestamp before each group of
  passes): at 64 cells the pressure 1.96 ms of 2.54, at 128 3.96 of 6.05. Two hypotheses: the multigrid's ~80
  dispatches per volume and step wait on each other (the coarse levels are a few hundred cells), and most cells are
  still air.
* **The small levels in one workgroup** (MG_TAIL, `gas_mg_tail_cells`): the coarser levels moved into one atlas
  texture per field (addressed by an x offset; the per-level passes unchanged, the state bit for bit), then the levels
  below a threshold solved by one workgroup of 256 with barriers. Bit for bit the same state; on the RTX slower at
  every threshold but 2048 cells at 64 (−2.0 %). Off.
* **Sparse bricks** (0119), the NumPy reference first: the masks from the bricks' maxima, every pass's result kept
  only where the GPU writes it, ambient values elsewhere. Its study: the campfire after 60 steps with a margin of 2
  bricks simulates 37 % of the bricks (the velocity 53 %), the smoke 1.9 % from dense (relative L2), the residual the
  same.
* **The closed box's divergence.** The first version made a coarse cell a zero cell (zero pressure) only where all
  its children were outside the simulated bricks. In a closed box (`gas_check.gd --case=closed --sparse=1`) the
  coarser levels then had no zero cell left, their problem was the closed box's singular one with an incompatible
  right-hand side, and the V-cycle diverged (the pressure 480 after 10 steps, the velocity at its clamp) — on the GPU
  and in the reference alike. A coarse cell is now a zero cell where any child is.
* **From the RTX profiles** (`m20_f263`, `m21_f264`): the activation read (2 margin + 3)³ sources per brick (now
  from shared memory); the first coarser level was dense (now over its bricks); and the torch — a world-space volume
  shifting its box almost every step at 128 cells — had every brick outside the simulated ones cleared by one
  invocation per brick after each shift (now one per texel, `SPARSE_CLEAR`): the step's emission group 0.58 → 0.34 ms
  at 128 cells.

## 2. Verification (lavapipe)

`gas_check.gd --sparse=1` against `gas_compare.py` (0119's table): ≤ 3.1 × 10⁻⁶ after 30-60 steps (campfire,
sphere obstacle, wind, closed box, moving world-space box, SOR); with a margin of 1 brick 8.8 × 10⁻⁴ after 60 (a brick
crossing the activity threshold in one and not the other). Two runs bit for bit. Dense: every case bit for bit the
previous build's (also with the levels' atlas and MG_TAIL at 0, 4096 and 65536 cells). Fire Lab sparse against dense
(lavapipe, RTX captures `m20_f263_capture`, `_temporal`): the same fire, smoke, plume and the torch's trail. City Lab:
0 differing pixels.

## 3. RTX measurements

| job | build | what |
|---|---|---|
| `m20_f262` | `fellow195` | MG_TAIL at 0, 2048, 8192, 32768 cells and SOR at 64 and 128 cells; the profile |
| `m20_f263` | `fellow196` | sparse (margin 2, 1, with SOR) against dense at 64 and 128; the profile; captures |
| `m21_f264` | `fellow197` | the activation from shared memory, the first coarser level over its bricks; sparse the default |
| `m21_f265` | `fellow198` | the parallel clear after a shift (section 0's numbers) |

MG_TAIL (`m20_f262`, frame against the per-level passes): 64 cells −2.0 % / +21 % / +66 % (2048 / 8192 / 32768),
128 cells +7.8 % / +21 % / +38 %.

Where the step goes at 128 cells, dense → sparse (`m21_f265`): emission group 0.16 → 0.34 ms, advection 0.84 → 0.25,
forces 0.54 → 0.17, pressure 4.49 → 2.62, projection and bricks 0.72 → 0.29.

## 4. Open

* **The pressure solve's coarser levels** are dense below the first; at 64 cells the dispatches' latency dominates
  (SOR, with fewer and larger dispatches, costs 0.84 ms against the multigrid's 1.58 — for a residual about 10×
  higher). Candidates: the coarser levels over listed bricks too, the restriction to the first coarser level over its
  bricks.
* The next stage of the plan: liquids (FLIP / APIC on this grid and solver), two-way coupling with Jolt.
