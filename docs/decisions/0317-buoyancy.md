# 0317 — Buoyancy: bodies floating on the waves (FellowBuoyancy3D, the waves on the CPU)

Status: **KEEP**, a new node and new methods of `FellowWater3D` (nothing changes without them) · Branch
`fellow/geometry` · Node `FellowBuoyancy3D` (`modules/fellow_fluid/fellow_buoyancy_3d.*`); `FellowWater3D`'s waves on the
CPU (`get_surface_height`, `get_surface_velocity`) · Measured with `benchmarks/tools/buoyancy/` (new: `buoyancy.gd`
headless, `gpu_match.gd` and `demo.gd` on lavapipe).

Checks: the cloud's CPU (the course headless; the comparison with the GPU's waves and the pictures on lavapipe, the
CPU's Vulkan); no GPU.

## Problem

`FellowWater3D` draws lakes and seas with FFT waves computed on the GPU, and nothing could float on them: a crate, a
barrel, debris or a boat fell through the surface. Floating objects riding the waves are what makes water a place in a
game (Sea of Thieves, Assassin's Creed IV, Crest and Atlas for Unity and Unreal). Two pieces were missing: the surface's
height where a body is, on the CPU, where physics runs (the GPU's displacement read back would be a frame or more late
and stalls), and the forces.

## Change

* **The waves on the CPU** (`FellowWater3D`): the cascades of waves of 10 m and longer (160 and 27 m by default)
  evaluated from the GPU's own modes — the same spectrum, the same random amplitudes and phases (`water_h0_mode`, now
  shared with the GPU's upload), the same quantized frequencies — on a grid of 16, 32 or 64 per cascade (the smallest
  holding 99.5 % of its variance), two complex inverse FFTs per cascade (the horizontal displacement; the height and its
  rate of change), recomputed when a point is asked for at a new time. `get_surface_height(position)` inverts the
  horizontal (choppy) displacement by fixed-point steps and returns the height drawn there (half a GPU texel's shift
  included); `get_surface_velocity(position)` the water's motion (the height's rate of change, the horizontal
  displacement's change since the last update). `-INF` outside the water's extent; `find_water()` picks the body of
  water under a point.
* **`FellowBuoyancy3D`**, a child of a `RigidBody3D`: its collision shapes filled with points (boxes, spheres, capsules,
  cylinders, convex hulls — the volume of each shape exact, shared by its points; other shapes as boxes), at most
  `max_points` (64). Every physics tick, each point under the surface (its cell's height for the share under) is pushed
  up by Archimedes (`water_density` × gravity × its volume under water) and dragged towards the water's motion
  (`linear_drag` 3/s as its share of the body's mass); the turning slowed by `angular_drag` (3/s) in the share under
  water. Forces at the points: the body floats at its draft, rights itself or capsizes as its shape dictates, and rides
  the waves. A sleeping body is woken while waves move it.

## Measurements

**The CPU's waves against the GPU's** (`gpu_match.gd`, lavapipe; the displacement texture read back, compared at the
texels the CPU's grid has, at the GPU's time):

| sea | Hs | cascade 160 m: height rms / CPU error rms | cascade 27 m | share of the variance on the CPU |
|---|---:|---:|---:|---:|
| 6 m/s wind, 3 km fetch | 0.21 m | 2.3 / 1.2 cm | 4.7 / 0.1 cm | 92 % |
| 12 m/s wind, 60 km fetch | 2.33 m | 58 / 1.3 cm | 4.1 / 0.08 cm | 99.9 % |

(The first version negated every wave: its centring sign was on the wrong cells; the comparison found it at once —
the error was exactly twice the waves.) The 5 m cascade and shorter are left out (0.75-0.9 cm rms of ripples).

**The buoyancy course** (`buoyancy.gd`, Jolt, 60 Hz, 20 s; the last quarter):

| still water | displaced volume vs mass / density | rest | settled in |
|---|---:|---|---:|
| boxes of 250, 500, 750 kg/m³ | −0.009 %, 0.000 %, 0.000 % | centres at +0.125, 0.000, −0.125 m (exact) | 11.6, 6.1, 4.8 s |
| sphere, barrel on its side, capsule, convex wedge | ≤ 0.0001 % | — | 6.0, 7.7, 5.9, 6.0 s |
| box of 1 500 kg/m³ | — | sinks (1.1 m/s) | — |

* Stability as the theory of floating bodies has it: a raft tilted 30° rights itself (0.0°, 5.6 s); a post (0.4 × 1.6
  × 0.4 m, half the water's density) lies down (90°); a cube of half the water's density floats edge up (45.0°) — a
  cube between 0.21 and 0.79 of the water's density is unstable face up.
* The sea (12 m/s, 60 km fetch, Hs 2.33 m): a buoy (0.25 m, 300 kg/m³) heaves with the surface under it (correlation
  0.997, 6 cm rms off it); a 3 × 2 m raft 0.999, 2.8 cm, tilting 6.7° on average; twenty crates on the surface (the
  worst 14 cm off it). With `follow_waves` off (drag against still water) the buoys follow alike on this course.
* The drag: 1/s — boxes settle in 13-20 s (bobbing for long; the lightest not within 20 s); 2 — 7-17; **3 (kept)** — 5-8 (12 for the lightest); 5 —
  3-7, but a sinking box falls at 0.65 m/s.
* The CPU: 4.3 µs a body a tick (34 bodies, 543 points); the waves 0.32 ms an update (two cascades, 64 × 64, once a
  physics tick while something floats; an editor build), 1.4 ms in the first version (three cascades, three transforms,
  a sine and cosine for each mode).
* The GPU's waves unchanged by the spectrum's per-mode code shared with the CPU: the regression suite's water and rain
  scenes identical to their goldens (lavapipe).
* Pictures (`demo.gd`, lavapipe): crates, barrels, a raft, a post and a red cube on a 9 m/s sea — riding the waves,
  the cube edge up, their parts under water seen through the refraction.

## Decision

KEEP: the waves on the CPU as an API of `FellowWater3D` (floating, swimming, a boat's own controller can use it), and a
node that floats a rigid body on them.

## Rejected / not done

* Reading the GPU's displacement back: a frame or more late, a stall or a copy every frame; the CPU's grid is the same
  waves, measured within 1.3 cm.
* Every mode of every cascade on the CPU (256 × 256 × 3): 1.4 ms and more an update for ripples that do not move a
  crate.
* A `FellowShallowWater3D`'s simulated surface (rivers, wakes): on the GPU; floating on it needs its read back — not
  done.
* Wave-making damping, added mass, slamming, a boat's hull forces (planing): the drag stands in for the water's
  resistance; a boat's controller is the game's.
* The waves' update on a worker thread (0.3 ms on the main thread while something floats): the next step if it
  shows.
