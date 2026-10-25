# 0117 — Gas simulation: smoke and fire on the GPU (FellowGasVolume3D)

Status: **KEEP as a feature** (milestone 20; a node a project adds: without one nothing runs) · Branch `fellow/geometry`
· Nodes `FellowGasVolume3D`, `FellowGasEmitter3D` (module `fellow_fluid`) · Settings `rendering/fellow/fluid/enabled`
(simulation), `rendering/fellow/fluid/gas_rendering` (decision 0118) · Code: `modules/fellow_fluid/fellow_gas_*`,
`servers/rendering/renderer_rd/effects/fellow_gas.*`, `shaders/effects/fellow_gas_sim.glsl`; reference
`modules/fellow_fluid/tests/gas_reference.py`, check `benchmarks/project/bench/gas_check.gd` +
`modules/fellow_fluid/tests/gas_compare.py`; bench scene Fire Lab (`--scene=fire`).

## Problem

Godot has no fluid simulation: smoke and fire are GPU particles with billboards (Particles Lab), which neither flow
around objects nor rise as hot gas, and whose look is the texture's. The user asked for a built-in simulator of
fluids, smoke and fire (2026-10-25). Unreal has Niagara Fluids (grid gas, an editor for it), EmberGen and Houdini's
pyro solver are the offline references; all are Eulerian grids for gas.

## Change

A box of air on a regular grid, simulated on the GPU, one step per physics tick (60 Hz by default: the same steps for
the same ticks, whatever the frame rate). The volume's node sits at the box's floor centre; `size` in metres,
`resolution` cells along its longest side.

**State** (per volume): the scalars at the cell centres — smoke density, temperature above ambient (K), fuel, burn rate
(fuel per second, this step) — in an RGBA16F 3D texture (RGBA32F with `high_precision`), two of them alternating; the
velocity on a staggered (MAC) grid, one (nx + 1) × (ny + 1) × (nz + 1) texture whose x, y and z channels are the x, y
and z faces' velocities (the texels past a component's last face repeat it, so that clamp-to-edge trilinear sampling
extends the field with a zero gradient); the pressure (R32F, warm-started from the last step); the solid cells (R8UI:
the obstacle's index); per 4³ brick the largest smoke and temperature (RG16F, for the renderer's empty space).

**A step** (13 kinds of compute pass, 13 + 2 × `pressure_iterations` dispatches):

1. *Obstacles*: the cells inside the physics bodies' shapes in the box (spheres, boxes, capsules, cylinders, world
   boundaries; a convex shape's box), with their linear and angular velocity, from one shape query of the physics
   server per tick (`collision_mask`; at most 16, the nearest first). Concave and height-map shapes are not obstacles:
   a volume standing on the ground closes its floor (`closed_faces`, the floor by default). A body faster than the grid
   carries (six cells per step) counts as teleported and does not push the gas: a kinematic body whose position a
   script sets is reported by the physics server with the jump as one step's velocity (Fire Lab's sphere: 100 m/s once),
   and that one step's pressure, warm-starting the following steps, blew the campfire's flames sideways for more than a
   second — SOR's slowly converging large scales (a multigrid solve would remove them in a step or two).
2. *Emission*: each `FellowGasEmitter3D` child (sphere, box or cylinder; at most 16) adds smoke and fuel per second,
   heats the gas to its temperature (or adds heat per second), optionally with value noise drifting upwards through
   it (a flame's flicker); in the face pass below it pulls the velocity towards its own.
3. *Advection* of the scalars and of the velocity: semi-Lagrangian back-tracing through the trilinearly sampled velocity,
   then MacCormack's correction (Selle et al. 2008: the forward and backward steps' error halved) clamped to the eight
   values the back-traced point was interpolated from. Through an open face fresh air comes in (zero).
4. *Combustion*, in the same pass: where the gas is hotter than `ignition_temperature`, a fraction of the fuel burns
   per second (`burn_rate`), releasing heat (`heat_release`, K per unit of fuel) and soot (`soot`, smoke per unit), and
   its rate is kept for the expansion; smoke dissipates (`smoke_dissipation`), the gas cools (`cooling`).
5. *Forces*: buoyancy g · T / (T_ambient + T) upwards (hot gas rises, the ideal gas's; `buoyancy` scales it) minus
   `smoke_weight` per unit of smoke; vorticity confinement (Fedkiw et al. 2001: ε h (N × ω), `vorticity` = ε), which
   puts back the swirls the grid's numerical diffusion takes; optional `turbulence`, a value-noise force where the gas
   is hot; the wind (`wind`, `wind_rate`: a relaxation towards it).
6. *Projection*: the divergence of the velocity against the solids' velocities and the walls, minus the combustion's
   `expansion` (burning gas expands: the fire's puffs), then the Poisson equation for the pressure (warm-started from
   the last step) and its gradient subtracted. Faces: closed (a wall), open (zero pressure outside) or, open with the
   wind blowing in, an inflow at the wind's velocity (fresh air). Two solvers (`pressure_solver`):
   - **multigrid** (the default): `multigrid_cycles` (2) V-cycles — at each level two red-black Gauss-Seidel sweeps
     (ω 1.15), the residual restricted to a level of half the size (averaged over the fluid children; a coarse cell
     solid where all its children are), down to a smallest side of at most 4 cells solved by one workgroup (20 sweeps),
     the corrections interpolated back trilinearly and two sweeps after each;
   - **SOR**: `pressure_iterations` (20) red-black sweeps, ω 1.6.
7. *Bricks*: the largest smoke and temperature per 4³ brick (with a one-cell border); per workgroup the fire's emission
   and its emission-weighted position, read back asynchronously (2-3 frames later): the node's omni light (decision
   0118) is placed and coloured from them.

8. *Export* (when `velocity_texture_enabled`): the velocity at the cell centres, v / `velocity_texture_scale` mapped to
   [0, 1], into an RGBA16F texture a `GPUParticlesAttractorVectorField3D` reads (2 × texel − 1 is its force): GPU
   particles carried by the flow (Fire Lab's embers).

**Space.** `space` = Local (the default): the gas moves and turns with the node (a fire on a ship). World: the gas
stays in the world and the box, axis-aligned, follows the node by whole cells — before a step in which the node moved,
a pass shifts the state by the offset into the other buffers, the cells and faces coming in getting fresh air (the
wind's velocity), the pressure's warm start dropped: a torch carried around trails its flame and smoke (Fire Lab's
torch). Checked against the reference: a move of (2, 0, −1) cells after 20 steps, 8.1 × 10⁻⁷ after 30.

The steps a node queues during the physics ticks run at the start of the next frame (inside its timestamps:
"FellowEngine Gas Simulation"), in the order of the volumes' creation.

## Verification

**Against the NumPy reference.** `gas_reference.py` is the solver written again in NumPy (float32, the same passes and
formulas, the reaction's factors computed as the CPU computes them). `gas_check.gd` runs a 32 × 48 × 32 volume (0.1 m
cells) in high precision on lavapipe and dumps its textures; `gas_compare.py` steps the reference as many times:

| case | steps | largest relative L2 difference (any field) | residual \|∇·u − e\| after the projection, GPU / reference |
|---|---:|---:|---|
| campfire (fuel, heat, floor closed) | 1 | 9.2 × 10⁻⁷ | 0.0211 / 0.0211 s⁻¹ (max) |
| campfire | 10 | 3.4 × 10⁻⁷ | 0.0022 / 0.0022 |
| campfire | 60 | 3.5 × 10⁻⁶ | 0.0171 / 0.0171 |
| a static sphere obstacle (r 0.5 m) | 30 | 9.0 × 10⁻⁷ (552 solid cells, the same) | 0.0077 / 0.0077 |
| wind 1 m/s through the x faces | 30 | 8.0 × 10⁻⁷ | 0.0113 / 0.0113 |
| closed box, warm smoke | 30 | 8.1 × 10⁻⁷ | 0.0027 / 0.0027 |

With the multigrid solver (the default since it was added; the table above is SOR's):

| case | steps | largest relative L2 difference | residual after the projection, max / mean (s⁻¹) | SOR's |
|---|---:|---:|---|---|
| campfire | 1 | 8.0 × 10⁻⁷ | 0.0073 / 0.0006 | 0.0211 / 0.0019 |
| campfire | 60 | 3.1 × 10⁻⁶ | 0.0019 / 0.00012 | 0.0171 / 0.0021 |
| a static sphere obstacle | 30 | 1.0 × 10⁻⁶ | 0.0010 / 0.00005 | 0.0077 / 0.00067 |

The reference's study, one projection of the campfire's state after 60 steps (the divergence before it up to 5.4 s⁻¹,
0.071 on average): SOR 20 warm-started 0.026 / 0.0032, cold 0.23 / 0.029; two V-cycles warm 0.0041 / 0.00025, cold
0.031 / 0.0027 (one V-cycle with piecewise-constant interpolation: max 0.27 — the trilinear interpolation and ω 1.15
chosen there). Two V-cycles are 8 sweeps of the grid and 4 passes besides (plus the coarser levels, about 1/7 more)
against SOR's 20 sweeps. The GPU is the reference to float rounding: the solver is what this record describes.

**Without a volume** nothing of it runs (no pass, no pipeline): the frame is the previous build's bit for bit (lavapipe,
City Lab, Crowd Lab, Lights Lab, City Lab with auto exposure; see the milestone report).

## Results

Fire Lab (lavapipe, 960 × 540): a campfire (2.4 × 4.8 × 2.4 m, 0.1 m cells at `--gas_res=48`) whose flames rise between
the logs (obstacles) and flicker, dark smoke above them; a chimney's plume (6 × 9 × 6 m) blown sideways. RTX: the
milestone report.
