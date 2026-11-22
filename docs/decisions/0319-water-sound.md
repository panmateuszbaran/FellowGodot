# 0319 — Water's sound synthesised from its bubbles (splashes, laps, wakes)

Status: **KEEP** · Branch `fellow/geometry` · `modules/fellow_fluid/fellow_water_sound.*` (new: the synthesis, an
`AudioStream` and its playback, internal); `FellowWater3D`'s voices and `sound_*` properties, `play_splash`,
`add_wake_sound`, `get_sound_stats`, `debug_render_sound`; `FellowBuoyancy3D.sound`; `AudioServer::finish` (a stock
file: the playbacks still listed at exit freed) · Measured with `benchmarks/tools/buoyancy/water_sound.gd` and
`water_sound.py` (new: the synthesis offline), `sound_timing.gd` (new: Movie Maker), `interaction.gd`,
`interaction_demo.gd` (now with sound).

Checks: the cloud's CPU (the synthesis offline and the courses headless; Movie Maker on lavapipe, the CPU's Vulkan); no
GPU. Nothing was listened to here: the physics are checked on spectra and envelopes; whether it sounds right is the
owner's ear.

## Problem

Decision 0318 made floating bodies splash and leave wakes on the water — silently. Water's sound in games is samples
(a splash, a loop of lapping) chosen by size and crossfaded; they repeat, do not follow what struck the water or how
fast, and a crate broken over a lake into twenty pieces needs twenty of them. The owner asked for high-quality sound
generated procedurally.

## Change

Water's sound is the sound of air bubbles (Minnaert 1933; Leighton, *The Acoustic Bubble*): what strikes water
entrains bubbles, and each rings once as a damped oscillator. The synthesis follows van den Doel's "Physically based
models for liquid sounds" (ACM TAP 2005):

* **A bubble** of radius r rings at Minnaert's frequency f = 3.26 m/s / r (1 mm: 3.3 kHz, 1 cm: 330 Hz), its amplitude
  decaying at d = 0.13 / r + 0.0072 r^-1.5 per second (viscous, thermal and radiation losses), its pitch rising as it
  nears the surface, f(t) = f0 (1 + ξ d t) (ξ 0.05-0.3, at most +60 %). A decaying rotor a sample (four multiplies),
  its step updated every 32 samples; its onset over 8 samples (a click's edge softened).
* **A splash** (a body of size s striking at speed v): a slap (noise through a band-pass, brighter for a small body,
  0.8 ms rise, 6-40 ms fall), a thump for a large one (a low band), a cloud of bubbles — 6 + 50 s v/5 of them (up to
  300), radii distributed as r^-2.2 from 0.4 mm up to 1.5 + 5 s v/5 mm, entrained over 25-115 ms — and behind a large or
  fast body its cavity closing (a torn pocket of air: a bubble of 4-20 mm damped 2.5 times as much, 30-350 ms later).
  Loudness 0.1 v √s.
* **A lap** (the water closing over a body already in it: a wave, its bobbing, it going deeper): a splash of its
  closing points, softer. **A wake** (its waterline moving through the water): a stream of small bubbles (60 a second
  per unit of the waterline's length times its speed) and the rush of the water (two bands of noise).
* **Voices.** `FellowWater3D` keeps `sound_voices` (8) `AudioStreamPlayer3D`s, made at its first sound, each playing
  its own synthesis on the audio thread (a custom `AudioStream`; the main thread posts events under a lock and the mix
  takes them). A splash joins a voice sounding within 2.5 m, else a free one, else the quietest if it is louder; wakes
  within 4 m share one; nothing farther than `sound_max_distance` (60 m) from the listener. The voices play until 10 s
  of silence (a playing voice starts a sound at the next mix; a stopped one a physics tick later).
* **`FellowBuoyancy3D`** plays its body's: a splash when it strikes the water from out of it (its striking points'
  footprint and speed), laps, its wake; `sound` scales it.
* **`AudioServer::finish`** frees the playbacks still in its list at exit: players stopped as the scene is freed wait
  for a mix that no longer comes, and were reported as leaked (8 at every exit with the voices; a stock player still
  playing at quit should have been too — not checked).

## Measurements

**Single bubbles** (`water_sound.gd` + `water_sound.py`, 48 kHz):

| radius | Minnaert | spectrum's peak | damping (model) | measured | pitch from 0.15 to 1 decay time (model ×1.084) |
|---|---:|---:|---:|---:|---:|
| 0.5 mm | 6 520 Hz | 6 520 Hz | 904 /s | 904 /s | ×1.066 |
| 1 mm | 3 260 | 3 260 | 357.7 | 357.7 | ×1.093 |
| 2 mm | 1 630 | 1 630 | 145.5 | 145.5 | ×1.077 |
| 5 mm | 652 | 652 | 46.4 | 46.4 | ×1.084 |
| 10 mm | 326 | 326 | 20.2 | 20.2 | ×1.077 |

(The rise measured in steps of 32 samples: within 2 % of the model.)

**Splashes** (each alone, its level as synthesised, before distance):

| what struck | peak | loudness (0.3 s RMS) | spectral centroid | below 300 Hz | rings (to −40 dB) |
|---|---:|---:|---:|---:|---:|
| 0.3 m at 1.5 m/s | −39.6 dBFS | −59.0 | 3 680 Hz | 0 % | 0.09 s |
| 0.3 m at 3 m/s | −33.8 | −51.2 | 3 910 | 0 % | 0.10 |
| 0.3 m at 5 m/s | −22.9 | −39.3 | 1 710 | 2.1 % | 0.15 |
| 0.3 m at 8 m/s | −15.3 | −33.9 | 1 640 | 2.4 % | 0.13 |
| 0.3 m at 11 m/s | −11.9 | −30.3 | 1 620 | 3.3 % | 0.14 |
| 0.1 m at 5 m/s | −28.1 | −50.2 | 6 730 | 0 % | 0.07 |
| 0.6 m at 5 m/s | −17.4 | −33.8 | 1 300 | 15 % | 0.21 |
| 1.0 m at 5 m/s | −12.0 | −28.1 | 990 | 37 % | 0.43 |
| 1.5 m at 5 m/s | −11.2 | −24.4 | 920 | 30 % | 0.57 |
| 20 pieces of 0.15-0.4 m at 10 m/s over 0.15 s | −1.3 | −16.8 | 1 470 | 5.8 % | 0.38 |

Louder the faster and the larger (29 dB from 1.5 to 11 m/s, 26 dB from 0.1 to 1.5 m), deeper and longer the larger (a
cavity's bloop from 0.6 m). A wake: −46, −36, −28 dBFS at levels 0.3, 1 and 3; silent 0.3 s after it stops.

**The CPU** (the audio thread): 0.05-0.13 ms a second of sound for a bubble, 0.08-0.85 ms for a splash (0.1-1.5 m),
4.0 ms for the twenty pieces (0.4 % of a core while it rings), 1.5 ms for a wake. In the interaction course's pond (a
crate dropped in, a raft pushed through it for 6 s, a crate rising out): 14 splashes and laps, 6 joining a voice, 1 549
bubbles (218 at once at most), 2.5 ms a second of sound. The main thread: the voices' bookkeeping (no synthesis). The
splashes of the still water and the sea, 100-200 m from the camera, dropped (1 158).

**When it is heard** (`sound_timing.gd`, Movie Maker at 30 fps; a box dropped into still water every second): the
first strike after silence heard 36 ms after the start of the frame it is drawn in (its voices start: a physics tick
later, and the next frame's mix); with the voices playing, 2-9 ms after (in the same frame). Both within what is not
noticed as late (sound up to about 45 ms after the picture).

**The demo** (`interaction_demo.gd` with Movie Maker, lavapipe): three crates breaking over a sea, a barrel, a raft
pushed through — each splash where it strikes, the pieces' laps, the raft's wake; peak −11 dBFS. The video with its
sound is for the ear.

**Unchanged:** the buoyancy course of decision 0317 (every number), decision 0318's course (its debris, foam, waves),
the regression suite's water and rain (identical to their goldens); no instance leaked at exit.

## How it got there

* The first spectra showed a 5 mm bubble's pitch sweeping from 650 to 1 500 Hz over its 0.3 s of ringing (the model's
  linear rise unbounded) and the cavity of a 1 m body a 0.8 s rising "whoop": the rise capped at +60 %, the cavity
  damped 2.5 times a bubble of its size (it is a torn pocket, not a sphere) and at most 2 cm.
* Twenty pieces striking together peaked at 0 dBFS (the soft limit taking what is over 0.6): the synthesis' level
  lowered 4.4 dB.
* Every bubble starting at full amplitude made a wake hiss up to 12 kHz (each onset a click): an 8-sample onset.
* Floating bodies lapping in waves made a steady rush (the bobbing counted as water moving past): bobbing now laps —
  small splashes where the water closes over points — and only motion through the water makes the rush.
* The voices stopped after 1 s of quiet: every new splash waited a physics tick for its voice to start (+36 ms); they
  play until 10 s of silence.
* 8 `FellowWaterSoundPlayback`s were reported leaked at every exit: Godot's `AudioServer` never freed the playbacks
  waiting for deletion when it finished (stock behaviour); fixed there.

## Decision

KEEP: water sounds as it is struck, with no samples, in a node that already existed (a water nothing strikes is
silent and costs nothing).

## Rejected / not done

* Samples crossfaded by size and speed: what games usually do; a game can still play its own on `water_entered`.
* A bubble's sound from a fluid simulation (Zheng and James' harmonic fluids, Moss et al.): offline or a GPU's work;
  the bubbles' population here is a model, not simulated.
* Rain on the water, a waterfall, a river's babble: the same synthesis could make them (FellowRain3D, FellowShallowWater3D)
  — not wired.
* The sound under water (the listener submerged: muffled, the bubbles closer): not done.
