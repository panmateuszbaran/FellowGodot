# 0318 — Water interaction: debris floats, splashes and wakes leave foam, the waves off the main thread

Status: **KEEP** · Branch `fellow/geometry` · `FellowDestructible3D.debris_floats` (`modules/fellow_world/`);
`FellowBuoyancy3D.foam`, `water_entered` / `water_exited`, the skip above the waves (`modules/fellow_fluid/`);
`FellowWater3D.add_foam`, `get_foam`, `body_foam_size`, `body_foam_decay`, `get_surface_ceiling`, the CPU's waves on a
worker thread · Measured with `benchmarks/tools/buoyancy/interaction.gd` (new, headless) and
`interaction_demo.gd` (new, lavapipe).

Checks: the cloud's CPU (the course headless; the pictures, the regression suite's water and rain on lavapipe, the
CPU's Vulkan); no GPU.

## Problem

Decision 0317 floated bodies on `FellowWater3D`'s waves, but nothing else in the scene knew about the water:

* **Debris fell through it.** `FellowDestructible3D`'s pieces are plain rigid bodies: a crate broken over a lake or a
  boat's planks went through the surface as through air, at 14 m/s to the bottom.
* **The water did not show what struck it.** A crate dropped in, a raft pushed along: no splash, no wake — the surface
  as if nothing touched it. AAA water marks what floats in it (Sea of Thieves' and Assassin's Creed IV's wakes, Crest's
  foam from objects, Unreal's water body interactions).
* **The waves cost the main thread 0.3-0.4 ms a frame** while anything floated (decision 0317's next step).

## Change

* **Debris floats** (`FellowDestructible3D.debris_floats`, on by default): while the scene has a `FellowWater3D`, each
  piece thrown out gets a `FellowBuoyancy3D` (its points a third of the piece apart, at most 16). Pieces lighter than
  water float at their density's draft (wood, the default density, 60 % under), heavier ones sink slowed by the water.
  Without water in the scene: nothing (no node added).
* **Foam made by what floats.** `FellowWater3D.add_foam(position, radius, amount)` adds foam on a 256 × 256 grid over
  `body_foam_size` (64 m: 0.25 m cells) around the camera — each cell the world's cell modulo the grid, so the window
  follows the camera without copying (the cells it leaves are cleared). It fades by `body_foam_decay` (0.3/s), only
  over the cells foam was added to; while there is none nothing is computed or sent. The surface's shader reads it
  through the foam's own noise: an amount of 0-1 drawn as 0.42-0.78 of the foam's coverage (a few bubbles to lace,
  never a solid sheet). `FellowBuoyancy3D` adds it: a point striking the water faster than 1 m/s splashes round it
  (more the faster, each point's splash a little off and of its own size: an irregular patch); a point at the waterline
  moving through the water faster than 0.4 m/s leaves a wake (wider the faster); `foam` scales both. A body found under
  the surface the first time it is seen (made or set there) does not splash. `water_entered(speed)` and
  `water_exited` tell a game when to play its sound or particles; `get_foam(position)` reads the foam.
* **The waves on a worker thread.** After a frame's process, if something asked for the surface in that frame or the
  one before, the CPU's waves for the frame's time are made on a worker; the next frame's physics ticks find them done
  (a wait if not). The modes are built on the main thread (a new spectrum), the worker reads nothing else of the node.
* **Above the waves: skipped.** `FellowWater3D.get_surface_ceiling()` (the level plus six times the CPU waves' rms
  height): a body whose lowest point is above it is out of the water without the waves evaluated.

## Measurements

The interaction course (`interaction.gd`, Jolt, 60 Hz, 14 s; `--frame_work_ms=4` spins 4 ms of other work on the main
thread after the nodes' process, as a game's frame has):

**Debris** (1 m crates, 24 pieces, prefractured, broken in the air 5.5 m over the water; the last 2 s):

| crate | afloat | sunk | the water displaced against the pieces' mass | reaching the bottom (6 m) at |
|---|---:|---:|---:|---:|
| wood (600 kg/m³), still water | 24 of 24 (mean share under 0.600) | 0 | +0.00 % | — |
| wood, a 12 m/s sea (Hs 2.3 m) | 24 of 24 (0.571) | 0 | −4.7 % (riding the waves) | — |
| stone (2 400 kg/m³), still water | 0 | 24 | — | 3.4 m/s at most |
| wood, `debris_floats` off (as before) | 0 | 24 | — | 14.4 m/s (as through air) |

The pile as a whole displaces its weight of water exactly; single pieces resting on others float higher or lower
(0.05-0.88 under), as a pile of debris does. 0.7-0.9 µs a piece a tick in still water, 3.8 µs in the sea (142 points
for 24 pieces).

**The waves' update** (the sea's own clock; 840 updates in 14 s, 839 on the worker): 0.4-0.46 ms on the worker; the
main thread's time in them 1.9-10 µs an update with 4 ms of other work after the process (0.32-0.34 ms before: the
update was made in the first physics tick that asked). With no other work at all (a headless loop: the next tick starts
at once) the main thread still waits 0.17-0.33 ms — a frame with nothing to draw has nothing to overlap. The modes'
build (a new spectrum) 21 ms once, on the main thread, as before.

**Out of reach:** crates on a pier 5 m over the 2.3 m sea — every tick skipped, 0.2 µs a tick (7.3-8.2 µs floating);
on a pier 2.5 m over it (within six rms heights) 5.2 µs: evaluated, out of the water.

**Foam** (a pond, the camera over it):

* a crate (0.8 m, 400 kg/m³) dropped from 4 m: `water_entered` at 8.1 m/s; 110 cells (6.9 m²) of foam at once,
  fading to 74 % in 1 s; it bobs out and comes back in at 2.5 m/s (a second `water_entered`);
* a 2 × 1 m raft pushed at 3 m/s for 6 s: a trail of 1 081 cells (68 m²: 18 m by 3-4 m), fading after it stops (to
  19 % in 6 s);
* a crate made 1.5 m under the surface going down at 3 m/s: no foam round it until it came back down after rising out
  (with the first version, which took a body's points as out of the water until seen: 0.995 at once — a false splash;
  the pictures showed it as splashes appearing late where crates broken under water threw pieces up);
* the foam grid: 15-38 µs of the main thread a frame while there is foam (95 µs before only the cells with foam were
  faded), a 64 KB texture update.

**Unchanged:** the buoyancy course of decision 0317, every number the same; the regression suite's water and rain
identical to their goldens (the shader's foam read only while there is foam); the destruction check passes (no water:
no buoyancy).

**Pictures** (`interaction_demo.gd`, lavapipe; `--before` as milestone 175): three crates dropped and broken over a
6 m/s sea, a barrel dropped in, a raft pushed through. Before: the pieces go under and are gone, the water untouched.
Now: splashes of foam where they strike (in the frame they touch it), the pieces floating in piles, the barrel ringed
with foam, the raft's wake trailing 15 m behind it and fading.

## How it got there

* The foam drawn as an amount added to the waves' foam: the foam's noise covers nothing below 0.4 and everything above
  0.9 — splashes were solid white discs, trails invisible a raft's length behind. Its coverage curve measured on the
  noise (0.5 → 10 %, 0.6 → 27 %, 0.7 → 54 %, 0.8 → 79 %) gave the mapping 0.42 + 0.36 √amount.
* The foam fading with the waves' `foam_decay` (0.6/s): the wake was gone a raft's length behind; a decay of its own,
  0.3/s.
* Splashes from every point the same: discs; each point's splash jittered in place and size.
* The first demo's crates broke under water (thrown from too high, broken late): their pieces' first tick was 4 m
  down, taken as striking the water — foam popped up 0.2 s after the crate had gone in. A body's points start as under
  until seen out of the water; the course checks it.

## Decision

KEEP: debris floats where there is water; foam from splashes and wakes; the waves off the main thread; bodies above
the waves skipped.

## Rejected / not done

* Spray (particles thrown up by a splash) and sound: the signals give the game the moment and the speed.
* Waves made by bodies (a wake's Kelvin pattern, a splash's ring of waves): the surface is the FFT's; the
  `FellowShallowWater3D` simulation could take impulses where it covers the water (`add_impulse`) — not wired.
* Foam carried by the water's flow: it stays where it was made (the waves' orbital motion averages out).
* Floating on a `FellowShallowWater3D`'s simulated surface (rivers): on the GPU; needs its read back.
