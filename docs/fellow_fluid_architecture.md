# FellowEngine fluid architecture

Status: **tier 2 (shallow water) implemented as a prototype** in `modules/fellow_fluid/`; tiers 1 and 3 are
designed here and intentionally **not** implemented yet (milestone 1 rule: do not start with 3D fluids).

Jolt stays responsible for rigid bodies, characters, joints, ragdolls and collisions. Fluids are a GPU-side
secondary simulation with a strictly limited, one-way (or latency-tolerant) interface to physics.

## The hierarchy

| Tier | Water body | Method | Resolution / cost model | Status |
|---|---|---|---|---|
| 1 | Ocean, huge lakes | Spectral (FFT, Tessendorf/JONSWAP) displacement cascades + analytic Gerstner swell for gameplay queries | 3-4 cascades of 256² FFTs, ~0.3-0.6 ms; cost independent of world size | design only |
| 2 | Rivers, floods, harbours, shallow areas, puddle fields | 2D shallow-water equations on the GPU (`FellowShallowWater3D`) | N² grid, O(N²) per substep; 256²-1024² tiles, several tiles streamed around the camera | **prototype** |
| 3 | Pouring, buckets, splashes, waterfalls impact zones | Local particle method (PBF or FLIP/APIC in a small grid), surface via screen-space fluid rendering | ≤ 64-256k particles in a bounded volume, spawned on demand, budgeted | design only |

The engine never simulates an entire sea with particles: tier 3 is only ever a *local* detail layer that is
seeded from and drains back into tier 1/2 height fields.

### Coupling between tiers
* **1 → 2**: the ocean displacement at the boundary of a shallow-water tile is imposed as a height/velocity
  boundary condition (the SWE `inflow` path already supports prescribed boundary state); tier 2 writes its
  surface into a shared "water surface atlas" that rendering and gameplay sample instead of the ocean where present.
* **2 → 3**: breaking / high-foam regions (the SWE `foam` channel is exactly an activity mask) spawn particles
  with the local velocity; particles that settle are removed and their volume added back to the SWE depth.
* **Rendering**: all tiers output to the same surface representation (height + normal + foam + velocity) so one
  water material and one set of post effects (refraction, caustics, underwater fog) serves all of them.

### Interface to gameplay / Jolt (all tiers)
* **CPU → GPU** (every frame, compact): up to 16 colliders (position, radius, velocity) and 16 impulses per
  water body, 768 bytes. No per-particle or per-cell CPU work.
* **GPU → CPU** (never synchronous): buoyancy and "is under water" queries are served from an **asynchronous**
  readback of a small query buffer (e.g. 64 probe points) with 2-3 frames of latency, or analytically from tier 1.
  The simulation itself never waits for the CPU. (Not implemented in milestone 1: the prototype is one-way
  collider → water.)

## Tier 2 prototype: `FellowShallowWater3D`

### Numerics (`modules/fellow_fluid/fellow_swe.glsl`)
* Staggered (MAC) grid: depth `h` at cell centres, `u` on +X faces, `v` on +Z faces, `foam` at centres,
  all in one RGBA32F texture; bed height / solid obstacles in an R32F texture.
* Per substep (explicit, symplectic Euler):
  1. **ADVECT** — semi-Lagrangian back-trace of `u`, `v` (momentum transport) and `foam` (bilinear).
  2. **HEIGHT** — flux-form continuity with upwind depth; an **outflow limiter** scales the outgoing fluxes of a
     cell so it can never go below zero, which makes the scheme *exactly* mass conserving without clamping
     (verified by `modules/fellow_fluid/tests/swe_reference.py`, a NumPy port of the kernels: mass ratio 1.00000
     after 1200 steps of a dam break with obstacles; without the limiter +17 %). Impulses (drops) are added here.
  3. **VELOCITY** — acceleration by the free-surface gradient `-g ∇(h + b + head_collider)`, wet/dry rules
     (no outflow from dry cells, no climbing onto higher dry beds), solid walls, damping, collider drag,
     inflow / outflow boundaries, foam sources (convergence, speed, surface steepness) with exponential decay.
* Per frame: **SURFACE** — surface height, gradient (→ normals) and foam into an RGBA16F texture that the
  water material samples (vertex displacement + normals + foam).
* Colliders push water through an extra hydrostatic head (submerged depth of the sphere under the surface),
  i.e. only velocities change, mass is conserved; faces inside the collider are dragged towards its velocity.
* Stability: semi-Lagrangian advection is unconditionally stable; the gravity-wave CFL number at the benchmark
  settings is ≈ 0.47-0.53 (1024² over 64 m, 1.25 m channel + 0.35 m drops, dt = 1/120 s).
* Determinism: fixed `substeps × time_step` per processed frame; the benchmark runs with `--fixed-fps 60`.

### Resources
| Grid | State (3 × RGBA32F) | Terrain R32F | Surface RGBA16F | Total |
|---|---:|---:|---:|---:|
| 256² | 3.0 MiB | 0.25 MiB | 0.5 MiB | 3.8 MiB |
| 512² | 12 MiB | 1 MiB | 2 MiB | 15 MiB |
| 1024² | 48 MiB | 4 MiB | 8 MiB | 60 MiB |

Dispatches per frame: `3 × substeps + 1` compute passes (7 at 2 substeps). No readback.

### Rendering
A grid mesh (≤ 512² quads) displaced in the vertex shader; the fragment shader reconstructs the normal from the
simulated gradient, refracts the opaque scene (screen texture) with Beer-Lambert absorption over the refracted
water thickness, applies Schlick Fresnel to the refracted part and lets the engine's specular IBL / lights provide
reflections; foam is an albedo layer with higher roughness. Shoreline cells fade with water depth.

### Measured in the Fluid Lab (`fellow-m1_fluid_perf`, RTX 5070 Ti Laptop, decision record 0003)
`simulation GPU ms` and `surface generation GPU ms` from RenderingDevice timestamps inside the module;
`water rendering GPU ms` = viewport GPU time with water visible − hidden (simulation running in both);
`VRAM` = allocated simulation textures (exact, table above).

| Grid | Simulation (2 substeps, 120 Hz) | Surface | Water rendering 1080p / 1440p | VRAM |
|---|---:|---:|---:|---:|
| 256² | 0.071 ms | 0.005 ms | 0.86 / 1.09 ms | 3.8 MB |
| 512² | 0.206 ms | 0.014 ms | 1.05 / 1.34 ms | 15.0 MB |
| 1024² | 0.744-0.790 ms | 0.048 ms | 1.10 / 1.58 ms | 60.0 MB |

The simulation is well inside its budget; **rendering the water costs more than simulating it** (refraction needs a
screen-texture copy with mips, plus the displaced grid). That is the first optimisation target of this path.

### Known limitations of the prototype
* One tile per node, no tile streaming / LOD yet; no two-way buoyancy; no sub-grid waves (the surface is as
  detailed as the grid — tier 1 spectra or a detail normal map would be layered on top in production).
* Explicit time integration: very deep water at very fine grids needs more substeps (CFL); an implicit or
  "stable fluids"-style pressure solve would remove that limit at higher cost.
* Refraction uses the screen texture, so objects outside the screen are not refracted (standard SSR-style limit).
