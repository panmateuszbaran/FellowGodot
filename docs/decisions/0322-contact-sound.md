# 0322 — Bodies' contact sound: modal synthesis from the physics' contacts

Status: **KEEP** · Branch `fellow/geometry` · `modules/fellow_fluid/fellow_contact_sound_3d.*` (new:
`FellowContactSound3D`, a child of a `RigidBody3D` — `material`, `wall_thickness`, `ground_material`, `mode_count`,
`sound_enabled`, `sound_volume_db`, `sound_bus`, `sound_unit_size`, `sound_max_distance`, the `impact` signal,
`get_modes`, `get_sound_stats`, `debug_render`; the synthesis `FellowModalSynth` on decision 0320's placed voice);
`FellowDestructible3D` (its pieces sound like its body) · Measured with `benchmarks/tools/world_sound/` (new:
`impact_sound.gd` offline and `impact_sound.py`; `impact_demo.gd`, a yard of falling things with Movie Maker).

Checks: the cloud's CPU (the synthesis offline, headless; the yard with Movie Maker on lavapipe, the CPU's Vulkan); no
GPU (the owner's question at the start: nothing left to measure there, the laptop's queue paused). Nothing was listened
to here: the physics and levels are checked on spectra; how it sounds is the owner's ear.

## Problem

FellowEngine's bodies collide, slide, roll, break (decisions 0182, 0188, 0246) and float (0317) in silence. A game plays
recorded impacts on `body_entered`, picked by hand per material and scaled by a guess of the speed — the same recording
for a plank struck at its end and its middle, for a crate tapped and dropped from a roof, nothing for a crate dragged
across a floor or a pipe rolling down a ramp, and the pieces of a broken crate (twenty at once) need a pool of
recordings of their own.

## Change

`FellowContactSound3D` makes a body heard from its own vibration (modal synthesis: van den Doel, Kry and Pai's
FoleyAutomatic), on the audio thread, no samples, placed at the body (an `AudioStreamPlayer3D` while it sounds, readied
when the body moves fast near the listener so its first strike is not late, stopped three seconds after it falls quiet;
at most 24 at once). Full scale is 120 dB SPL at a metre, `sound_unit_size` (1 m) where that is heard.

* **Modes** from the body's collision shapes (their box, whether round, their volume), its `material` (seven: wood,
  metal, stone, glass, plastic, rubber, ceramic — Young's modulus, density, Poisson's ratio, the loss factor from Cremer
  and Heckl, rounded) and `wall_thickness` (0 solid): a long body (2.5 times its width) bends as a free-free beam, f =
  beta^2 / (2 pi) c kappa / L^2 (beta L = 4.730, 7.853, ...; kappa the section's radius of gyration: a rod's, a tube's,
  a slab's, a box section's) in both planes; a slab bends as a plate, f = pi / 2 c h / sqrt(12 (1 - nu^2)) ((m / L)^2 +
  (n / W)^2); a hollow box's three pairs of walls each as plates of the wall's thickness; a can's or a bottle's shell in
  its ring modes (f = c h / (2 pi r^2 sqrt 12) n (n^2 - 1) / sqrt(n^2 + 1)). Each 3 % off at random (no body is ideal),
  the lowest `mode_count` (24) from 25 Hz. Each decays at pi f eta + 1.5 a second (eta the loss factor). Each mode's
  loudness from how well it radiates: rho c v sqrt(sigma S / 2 pi) at a metre for v its velocity, S the body's largest
  face, sigma (ka)^2 / (1 + (ka)^2) and under a plate's coincidence frequency its edges' share (Maidanik); 2.5 % of a
  strike's change of velocity rings in the modes (the rest moves the body).
* **A strike**: a contact closing faster than gravity's tick (and 0.3 m/s), new or twice as fast as before. Its change
  of velocity is the contact's impulse over the mass (Jolt estimates it with the contact). It drives the modes with a
  half-sine of force as long as the Hertzian contact, 2.87 (m^2 / (R E*^2 v))^(1/5) (the pair's reduced mass, a
  curvature of a sixth of the body's smallest size, both materials' stiffness): steel on stone 0.6 ms excites
  everything, rubber 9-21 ms only the lowest. Each mode weighted by its shape where the strike lands, cos(m pi u) cos(n
  pi v). The body's sudden slowing radiates a click: a dipole, rho V (da/dt) / (4 pi c r), no more than rho c dv a / 2
  once the body is not small against the wavelength, over how long the body as a whole takes to stop (a hollow box's
  wall or a hollow round body's shell gives way first: a plate's or a ring's stiffness, half a period) — a small stone
  is heard as this, its modes ultrasonic. The other body's material comes from its own FellowContactSound3D (a floor's,
  a table's: on a static body it only gives that) or `ground_material`.
* **Sliding and rolling**: a contact held with the slip at the contact point over 8 cm/s drives the modes with the
  surfaces' roughness passing under it — noise low-passed at v / 0.4 mm, its force the friction's (half the weight on
  that contact's slope) growing as v^0.8 (a rough surface's heights), the two materials' roughness — and a little of the
  scrape itself; a body rolling (slip under a third of its speed, its spin matching) likewise with features 4 mm apart,
  smoother. Touching something damps the modes (12 a second, 25 resting).
* **The `impact` signal** (where, how fast, against what) for games: breaking things, dust, letting guards hear.
* **Pieces**: a `FellowDestructible3D` whose body has a FellowContactSound3D gives each piece one of the same material
  (solid, 12 modes).

## Measurements

`impact_sound.gd` + `impact_sound.py` (48 kHz, offline; at a metre, full scale 120 dB SPL):

| body | lowest modes | contact (dropped on stone at 3 m/s) | peak dropped | its strongest lone mode's T60, measured / synthesised |
|---|---|---:|---:|---:|
| crate, 0.6 m, planks 2 cm (20 kg) | 211, 217, 220, 524 Hz | 1.44 ms | −6.0 dBFS | 211 Hz: 0.77 s / 0.73 |
| plank, 1.2 m × 15 × 3 cm | 88, 241, 432 | 1.10 | −1.1 | 88 Hz: 1.41 / 1.43 |
| steel pipe, 1 m, 6 cm, wall 3 mm | 390, 390, 1074 | 0.57 | −0.2 | 3.4 kHz: 1.15 / 1.20 |
| steel bar, 1 m × 3 × 3 cm | 151, 154, 423 | 0.81 | −10.0 | 2.1 kHz: 1.66 / 1.66 |
| steel sheet, 1 m × 0.6 m × 2 mm | 33, 57, 59 | 1.09 | −12.2 | 281 Hz: 3.67 / 3.72 |
| glass bottle, 30 cm, wall 3 mm | 1249, 1265, 1461 | 0.26 | −8.0 | 1.46 kHz: 0.81 / 0.82 |
| stone, 12 × 10 × 8 cm | (none under 20 kHz) | 0.53 | −3.6 (its click) | — |
| rubber ball, 20 cm | 481, 1223 | 8.52 | −32.7 | (under the measurement's 0.1 s) |

* **Beam theory**: the steel bar's lowest mode 156 Hz by theory, 151 synthesised (3 % off), found at 154; the pipe's
  381, 390, 390.
* **Where it lands**: the plank struck at its end has its second mode 2.0 dB over its first; in its middle (a node of
  the second) −15.1 dB.
* **How hard**: the crate dropped at 0.5, 1, 2, 4, 8 m/s: −35.7, −28.9, −21.9, −14.7, −8.4 dBFS (6.9 dB a doubling of
  the speed: the amplitude with the speed and a shorter contact, 2.06 → 1.19 ms, brighter: 377 → 628 Hz). Struck by
  metal: 1.48 ms, 426 Hz; by rubber: 21 ms, 111 Hz, 7.5 dB quieter.
* **Sliding** (the crate on stone): 0.3, 1, 3 m/s: −45.3, −35.0, −26.8 dBFS (18.6 dB a decade), 248, 506, 707 Hz. The
  pipe rolling at 1 m/s: −46.2 dBFS, 367 Hz.
* **The CPU**: 0.05 ms (a stone: its click alone) to 3.5 ms (a steel sheet ringing, sliding) of the audio thread a
  second of sound a body; a silent voice skips its modes.

**The yard** (`impact_demo.gd`, Movie Maker on lavapipe, 17 s, the camera 3.6 m away): 35 strikes found and signalled on
eight bodies — the crate (dropped, then pushed: 0.38 s sliding), the pipe (dropped on the ramp: 1.6 s rolling down and
off it, 7 strikes), three stones knocking each other (13), the bottle on the table, the bar, the ball's eight bounces,
the breaking crate (its impact signal broke it) — and 83 among its 14 pieces, each sounding; the loudest moment the
crate breaking, −6.3 dBFS. The pieces resting stop their voices.

**Unchanged:** the regression suite's destruction, structure, vehicle damage, fire and water scenes identical to their
goldens.

## How it got there

* The first levels clipped everything (a crate's click 135 dB at a metre, a bottle's 125): full scale moved to 120 dB
  SPL, and the click uses how long the whole body takes to stop — a hollow crate's wall gives way over 15 ms, its click
  25 dB down, while its walls still ring from the short contact where the blow lands (one duration for both had silenced
  the crate's modes: a dull thud).
* The plank's render took 25 ms a second: its fast modes decayed into denormal floats while its slow one still rang.
  Flushed below 1e-15.
* Scraping first barely grew with speed (+2.7 dB from 0.3 to 3 m/s: only its band widened) and stood 15 dB too loud: the
  drive as v^0.8, 15 dB down.
* Resting pieces kept their voices: the solver's jitter read as sliding. Sliding needs 8 cm/s of slip and the body
  moving.

## Decision

KEEP: bodies sound as what they are, struck where and as hard as they are, without recordings.

## Rejected / not done

* Recordings per material (`body_entered`): a project turns `sound_enabled` off.
* Modes from the mesh (a finite-element analysis): a body's box and construction give the right order of its modes (the
  beam's within 3 %); a model from the mesh is for hand-made objects of their own.
* A static body ringing (a metal floor struck): it only gives its material.
* Fracture's own sound (the crack as the crate breaks) beyond its pieces' impacts; squeaks (stick-slip), a body rattling
  inside another.
