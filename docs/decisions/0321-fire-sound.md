# 0321 — Fire's sound: the flames' roar from the simulated fire, the wood's crackle from its pyrolysis

Status: **KEEP** · Branch `fellow/geometry` · `modules/fellow_fluid/fellow_weather_sound.*` (the fire's synthesis,
`FellowFireSynth`; decision 0320's voice can be placed: an `AudioStreamPlayer3D`); `FellowGasVolume3D` (the flames:
`sound_enabled`, `sound_volume_db`, `sound_bus`, `sound_unit_size`, `sound_max_distance`, `get_sound_stats`,
`debug_render_sound`); `FellowBurnable3D` (the wood: the same and `sound_crackle`; `get_fuel_release`) · Measured with
`benchmarks/tools/world_sound/` (new: `fire_sound.gd` offline and `fire_sound.py`; `fire_demo.gd`, Burn Lab's campfire
with Movie Maker).

Checks: the cloud's CPU (the synthesis offline, headless; the campfire with Movie Maker on lavapipe, the CPU's Vulkan);
the demo's frame times on the RTX queued (job f539). Nothing was listened to here: the physics and the levels are checked on
spectra and counts; how it sounds is the owner's ear (he asked for fire after decisions 0319 and 0320).

## Problem

FellowEngine's fire is simulated — the gas volume's flames (decision 0117) and burning objects that pyrolyse, char and
glow (0120) — and silent. A game had to loop a campfire recording at the fire and fade it by hand, and a loop does not
know the fire is growing, that a crate beside it has caught, that fuel was thrown on, or that only embers are left.

## Change

Two sources, as in a real fire, each synthesised on the audio thread with no samples and placed where it is (an
`AudioStreamPlayer3D` the node owns while it sounds, stopped two seconds after it falls silent or the listener goes
beyond `sound_max_distance`):

* **The flames** (`FellowGasVolume3D`). A flame is a monopole source: its sound is the rate of change of its heat
  release (Strahle; Bragg). The heat release comes from the simulated fire: the flames' volume is the fire's emission
  summed over the cells (already read back for the fire light; now also with the light off) over `fire_intensity`, and
  turbulent flames release about 1.2 MW in each cubic metre (Orloff and de Ris); its width a flame about 2.5 times as
  tall as wide. Burn Lab's campfire: 0.05 to 0.9 MW (1 to 19 campfires of 50 kW), 0.3 to 1.1 m. The sound: pink noise
  between two-pole high- and low-passes at 1 and 1.5 times 160 / sqrt(D) Hz (a peak at 0.94 of it, 2.7 octaves wide),
  its amplitude the root of the heat release (a constant acoustic efficiency), puffing at 1.5 / sqrt(D) Hz (pool fires'
  measured rate, Cetegen and Ahmed) by 22 % with the simulation's own flicker on top (the power eased over 0.12 s); the
  flames' flutter (a band at 0.6-1.1 kHz moving a dozen times a second) and the hot gas's hiss (3-9 kHz), faint; a
  flare's "whoomp" — low noise (two poles at 90 Hz), 30 ms up, 0.25 s down — when the fire grows much faster than it was
  (the posted power above its 0.5 s average by 0.4 + half of it; at most every 1.5 s).
* **The wood** (`FellowBurnable3D`). Wood crackles where it pyrolyses: pockets of steam and gas bursting at the front.
  Each crack a broadband snap of 0.15-0.5 ms and a short ring (a band-pass at 1.5-7 kHz, Q 1.5-4, 1.5-6 ms); their
  energies a power law (Pareto 1.4, the amplitude its root, capped at 100: crackling noise), one in seven followed by a
  run of 1-4 weaker ones 8-50 ms apart; one in fourteen a pop of sap (220-640 Hz, 15-40 ms). Their rate follows the area
  pyrolysing — the voxels releasing volatiles, a voxel's face each — at 15 a second for each square metre (at most 60;
  `sound_crackle` scales it): the burning runs `burn_speed` times faster than real wood, so its mass rate (Burn Lab's
  logs 20-400 g/s against a real log's grams) would crackle as fast. While the char glows (its share and the temperature
  over `glow_temperature`) the embers tick, three a second at most.

The fire's synthesis renders one channel (the placed voice pans it). `FellowGasVolume3D.debug_render_sound` renders it
offline from keys for tests.

## Measurements

`fire_sound.gd` + `fire_sound.py` (48 kHz, offline; heat release as a multiple of a campfire's 50 kW):

| flames | heat release | loudness | roar's peak (predicted 160 / sqrt(D)) | puffing (1.5 / sqrt(D)) |
|---|---:|---:|---:|---:|
| 0.25 m | 0.31 | −41.4 dBFS | 311 Hz (320) | 3.10 Hz (3.00) |
| 1 m | 9.9 | −26.2 | 158 (160) | 1.55 (1.50) |
| 4 m | 316 | −13.0 | 94 (80) | 0.73 (0.75) |

(A campfire's 50 kW about −36 dBFS at `sound_unit_size`, 3 m: soft under its crackle; 300 times the heat 25 dB louder.)

| crackle | asked a second (with the runs) | heard a second (above 2 kHz: 1 ms windows over 6× their median) | the tail of their peaks (survival, log-log) | peak, crest |
|---|---:|---:|---:|---:|
| alone, 30 s | 12 (16.5) | 14.2 | −2.52 (the synthesis: −2.8) | −1.3 dBFS, 41 dB |
| in a campfire (heat 1, 0.4 m) | 8 (11) | 9.5 | −2.11 (the smallest under the flames) | −2.4 dBFS, 33 dB |
| in a bonfire (heat 25, 1.5 m) | 40 (55) | 47.0 | −2.93 | −3.5 dBFS, 19 dB |

A gas catching (heat 0 → 6): the lows under 150 Hz 11.9 dB over the burning fire's in the 0.4 s after it (the flare).
Embers: 2.9 ticks a second, peaks −19 dBFS.

**The CPU** (a second of sound): 0.9-1.1 ms for flames, 1.0-2.0 ms with crackle (2.0 at 55 cracks a second).

**The campfire** (`fire_demo.gd`, Movie Maker on lavapipe, 20 s; Burn Lab's fire: kindling under two logs and a branch,
a crate lit at 3 s, the kindling out by 14 s, fuel thrown on at 11 s; the camera circling at 2.2 m): the flames' heat
from the simulation 1 → 19 campfires as the logs caught (0.4 → 1.1 m across), 0.3-1 once the kindling was out, 17.5 at
the fuel; two flares (the kindling catching at 1.5 s, the fuel at 11 s); the cracks asked 2 → 39 a second as the logs,
the branch and the crate caught, heard above 2 kHz 2 → 43 a second; the sound −35 dBFS (0-2 s) → −24 (the fire full, 4-8
s) → −39 (the flames low, the wood crackling, 16-20 s), peak −7.7 dBFS. Five voices (the flames, four objects): the
audio thread 1.2 ms (the flames) and 1.2-1.6 ms (each object) a second of sound.

**Unchanged:** the regression suite's fire, water and rain scenes identical to their goldens.

## How it got there

* The first roar peaked at a third of its corner (pink noise under a one-pole high-pass peaks at the high-pass) and was
  the loudest thing in the fire (a campfire −23 dBFS): two-pole filters at 1 and 1.5 times the peak, the roar 9 dB down,
  the cracks 9 dB up, the flutter's band narrower and softer, the hiss 8 dB down and under 9 kHz.
* The crackle first followed the mass pyrolysed (6 cracks a gram): Burn Lab's logs release 20-400 g/s (the burning runs
  ten times faster than wood's and its model pyrolyses fast) and every object sat at the cap, 150 a second. The area
  pyrolysing at 25 a second a square metre gave the campfire 80 a second with the runs: 15 (and runs one time in seven,
  not four).
* The first count of cracks heard (a one-pole difference at 1.5 kHz) let the roar through and found none while the fire
  was large; a fourth-order high-pass at 2 kHz (the ear separates them so) finds what was asked.

## Decision

KEEP: a fire is heard as it burns — growing, flaring, crackling where its wood burns, ticking as embers — without a
recording.

## Rejected / not done

* Recordings at the fire: a project turns `sound_enabled` off and plays its own.
* The resolved part of the heat release's derivative as the roar: the emission is read back a few times a second; the
  audible band is synthesised, the simulation's flicker shapes its level.
* Sizzling where water or rain meets the fire, wet wood's whistle of steam, a fire's draught in a chimney.
* Crackle by species (a property, `sound_crackle`, not a material model).
