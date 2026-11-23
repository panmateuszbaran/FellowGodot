# 0323 — Footsteps synthesised: what the foot does, what it lands on

Status: **KEEP** · Branch `fellow/geometry` · `modules/fellow_fluid/fellow_footsteps_3d.*` (new: `FellowFootsteps3D`, a
`SkeletonModifier3D` last on a character's skeleton — its bones, `contact_height`, `heel_offset`, `collision_mask`,
`body_mass`, `shoe`, `default_ground`, `sound_*`, the `footstep` signal, `get_ground`, `get_foot_state`,
`get_sound_stats`, the static `debug_render`; the synthesis `FellowFootstepSynth`); `FellowContactSound3D.surface`
(what a foot on it stands on); `FellowTerrainLayer.surface` · Measured with `benchmarks/tools/world_sound/`
(`footstep_sound.gd` offline and `footstep_sound.py`, new) and `benchmarks/tools/foot_placement/footsteps_demo.gd`
(new: a CMU walk across thirteen grounds and over a terrain).

Checks: the cloud's CPU (the synthesis offline, headless; the walk headless and with Movie Maker on lavapipe); no
GPU. Nothing was listened to here: the physics and levels are checked on spectra and counts; how it sounds is the
owner's ear. The walk is CMU's motion capture (subject 35, trial 8; BVH from the una-dinosauria/cmu-mocap
conversion), not committed.

## Problem

A character's footsteps are a game's most heard sound and FellowEngine had none: a game sets animation events at
each foot's frames by hand (per clip, again when the clip changes; nothing for a blend of two gaits, a foot placed
on a step by `FellowFootPlacement3D`, a turn on the spot), picks a recording by a tag on the floor (one per material,
a few variations against repetition), and has nothing for how hard the foot comes down, the sole rolling onto the
ball, a toe dragged, the shoe.

## Change

`FellowFootsteps3D`, last in the skeleton's modifiers, reads the feet as drawn (after `FellowFootLock3D` and
`FellowFootPlacement3D`): each sole's heel and ball (FellowFootLock3D's points) against the ground under the foot (a
ray, the character's own body left out). A foot is down when its lower point is within `contact_height` of the
ground and planted (slower than 0.6 m/s across it: a swinging foot can pass as low, not as still); up again when
both points are 2 cm higher or swept away (1.2 m/s). Its touching down is a strike at the speed its point came down
over the last 0.1 s; the other point coming down after is the sole rolling onto it (softer); leaving, the toe lifts;
a foot moving across the ground while down scuffs. The ground: the ray's collider's `FellowContactSound3D` (its new
`surface`, or its material: wood a wooden floor, metal a steel deck, stone, glass, plastic and ceramic tiles, rubber
a mat), a `fellow_surface` metadata (a name or a number), a `FellowTerrain3D`'s layers there by their weights (each
layer's new `surface`; the two strongest mixed), or `default_ground`; a `FellowWater3D` over the ground there makes it
water. The `footstep` signal (which foot, where, on what, how fast) for games — guards hearing, tracks in snow, dust.

The synthesis (`FellowFootstepSynth`, one placed voice for the character):

* **Hard ground** (stone, wood, metal, tile, carpet): a half-sine of force as long as the sole takes to compress —
  a sneaker's 2 ms, a boot's 1, a hard heel's 0.35, a bare foot's 3 (Hertz: longer for a slower foot) — through the
  shoe's own resonance (0.3-2.6 kHz) and the floor's (a wooden floor's boards booming at 95-820 Hz, a steel deck's
  plate ringing for half a second, tiles clicking at 1.8 and 3.1 kHz; a concrete slab none), with the click of the
  force's onset (a dipole: the shorter the contact, the louder) and the weight's low thump.
* **Loose ground** (gravel, sand, snow, grass, leaves, dirt, mud): Cook's PhISEM — the step's energy, decaying over
  how long the weight keeps crushing (0.1-0.24 s), drives random collisions of grains at a rate with that energy
  (200-4000 a second at a walking step's), each a click of Pareto-distributed strength through the material's
  resonances (gravel 2.1, 3.4 and 5.2 kHz, snow 0.9-4.1 kHz, sand 2.8 and 5.6 kHz broad, dry leaves sharper and
  spikier), over the ground's compression; grass and leaves brushing; mud squelching (a band sweeping up as the foot
  sinks) and sucking at the toe lifting (a bubble rising in pitch); water splashing (decision 0319's bubbles, 6-13
  entrained, drips after the toe).
* **Scuffs**: grains rolling under the foot, or a rub through the floor's resonances; a sneaker on a smooth floor
  (wood, tiles) faster than 0.5 m/s squeaks (stick-slip: 1.3-2.3 kHz with its harmonics, wavering).
* A step's strength goes with the sole's speed and the root of the weight (`body_mass`); the second point a little
  over half, the toe's lift a third.

## Measurements

`footstep_sound.gd` + `footstep_sound.py` (48 kHz, offline; four walking steps: the heel at 0.5 m/s, the ball 80 ms
later, the toe; sneakers, 75 kg; heard so at `sound_unit_size`, 3 m: a third-person camera's distance; a step's
loudness is its K-weighted energy, ITU-R BS.1770's filter, against a step on stone):

| ground | peak | a step over stone's | spectral centroid | under 300 Hz / 0.3-2 kHz / 2-6 kHz / over 6 kHz | grains (bubbles) a step |
|---|---:|---:|---:|---|---:|
| stone | −26.7 dBFS | 0 LU | 204 Hz | 77 / 23 / 0 / 0 % | — |
| wood | −22.3 | +7.8 | 102 | 98 / 2 / 0 / 0 | — |
| metal deck | −25.6 | +12.0 | 229 | 96 / 4 / 0 / 0 | — |
| tile | −26.1 | +0.5 | 210 | 77 / 22 / 0 / 0 | — |
| carpet | −40.4 | −13.5 | 128 | 88 / 11 / 0 / 0 | — |
| gravel | −11.6 | +11.9 | 2 341 | 11 / 38 / 47 / 4 | 316 |
| sand | −26.1 | +6.8 | 3 140 | 7 / 32 / 49 / 12 | 1 320 |
| snow | −17.8 | +8.5 | 1 176 | 21 / 60 / 18 / 1 | 688 |
| grass | −36.8 | −3.0 | 6 413 | 16 / 13 / 31 / 40 | 574 |
| dry leaves | −11.2 | +11.1 | 3 325 | 6 / 25 / 58 / 11 | 182 |
| dirt | −32.3 | −0.9 | 127 | 94 / 6 / 0 / 0 | 37 |
| mud | −32.7 | +5.9 | 1 430 | 16 / 72 / 8 / 4 | 26 (1) |
| water | −25.3 | +12.0 | 6 341 | 0 / 37 / 25 / 38 | (21) |

* **Shoes** on stone: a sneaker 204 Hz, a boot 508, a hard heel 2 229 (36 % over 2 kHz: the click, 11 dB louder than
  a sneaker), bare feet 121.
* **Gaits** (against walking, peaks): sneaking −9.7 dB on stone, −9.4 on gravel; running +11.4 and +9.5; landing from
  a jump +20.0 and +11.6 (on gravel the voice's soft limiter: the landing reaches full scale at 3 m).
* **Weight**: 100 kg on gravel 6.1 dB over 50 kg (each step's energy with the root of the weight, the grains it
  crushes and their strength with it).
* **Scuffing** on wood: a sneaker's squeak 10 dB over the spectrum round it (2.06 kHz), a boot's 5 dB (no squeak).
* **Grass over dirt** (a terrain's mix, 60/40): the grass's brushing over the dirt's thump (57 % under 300 Hz, 21 % over
  6 kHz).
* **The CPU**: 0.3 ms (stone) to 6.9 ms (running on gravel) of the audio thread a second of steady walking; a step
  ends at −50 dB.

`footsteps_demo.gd` (CMU 35_08, a walk looped where its gait matches, across thirteen 3 m strips; Movie Maker on
lavapipe, 31.5 s; the camera 3.3 m away): every step found — 58, 3-5 on each ground — one every 0.535 s (median;
0.43-0.60, each foot's stride 1.00-1.14 s), the heel coming down at 0.30-0.34 m/s; the sound within a frame of the
foot (its onset 18 ms before the frame that reports it, the median). Each ground's steps there (median peak, the
step's centroid, its loudness over a step on stone's): stone −36.3 dBFS 169 Hz, wood −31.4 104 Hz +8.7 LU, metal −34.4
237 Hz +11.9, tile −35.6 198 Hz +1.1, carpet −50.3 102 Hz −12.8, gravel −29.9 1.9 kHz +9.0, sand −39.2 2.1 kHz +4.7,
snow −32.7 1.0 kHz +6.6, grass −49.8 2.7 kHz −6.5, leaves −24.7 2.3 kHz +8.2, dirt −42.3 88 Hz +1.1, mud −42.7 1.1 kHz
+4.7, water −38.0 2.3 kHz +7.8 (milestone 181b's balance; before it metal +19.5, mud +18.6, water +17.6); 4.1 ms of
the audio thread a second. With `--terrain` (headless) over a FellowTerrain3D whose grass layer blends into gravel
over ten metres: grass with 10 % gravel, then 50 %, then gravel with grass 40 % down to 10 % — 18 steps on grass, 16
on gravel.

## How it got there

* The first loose grounds took 20-67 ms of the CPU a second of sound: their resonances, long dead, decayed into
  denormal floats while each step lived on. Flushed.
* The first hard ground was all thump (centroid 72 Hz, 99 % under 300 Hz, no click: a hard heel's sole compressing
  over 1 ms excites nothing over 500 Hz) and stone had a floor resonance at 160 Hz: the sole's contact shortened to
  what a heel's edge takes (sneaker 6 → 2 ms, hard 1 → 0.35), the click as the force's onset with the contact's
  speed, concrete without a resonance, the shoe's resonance only on hard ground.
* The first grains were 26 dB too loud (gravel −2 dBFS, the scuff's −1) and grass hissed (a centroid of 9.7 kHz).
* In the walk at 3 m the steps were 20 dB under the offline ones (−53 dBFS on stone: the camera's distance, a slower
  heel): `sound_unit_size` 3 m (the level synthesised is a third-person camera's) and 6 dB more. A step's grains,
  thump and brushing went on far under hearing for a second: they end at −50 dB.
* In the walk the steps came late or not at all, in pairs at the loop: CMU's soles stand 3-5 cm over the ground
  (`contact_height` 0.05 for it, 0.04 by default), a swinging foot passing as low counted as down (a touch must be
  planted), and the second point of a foot down had made its strike a roll (a foot's state, not each point's).
* **Milestone 181b, the grounds' balance** (the owner's ear: mud and metal much louder than the rest). Each ground's
  loudness had come from its own model alone: in the walk a step on the steel deck was 19.5 LU over one on stone, in
  mud 18.6, in water 17.6, the next wood's 8.7 — offline the same (+19.5, +19.9, +21.9: the deck's plate ringing at
  Q 60-100, mud's squelch broad and 0.35 s long, the splash's bubbles). A ground's level is now what it radiates, an
  output gain (the grains it crushes and their rate stay the step's), set against the one measurement found of walking
  on different grounds (Aletta, Kang, Fuda, Astolfi 2016, *J. Environ. Eng. Landsc. Manag.* 24(3): binaural, 2 steps
  a second: stone 40.1 dB(A), wood 48.6, gravel 66.1, grass 28.5) and the physics where it has none: wood +7.8 LU over
  stone already (measured: +8.5); gravel 1.58 (+8.0 → +11.9: the loudest there, here kept with the deck, the water and
  dry leaves); grass 0.46 (+3.8 → −3.0: under stone, not as far); the steel deck 0.42 (+12.0: a few dB over a wooden
  floor — an 8 mm plate's point mobility 2.4 times lower than 22 mm boards', its loss factor ten times lower, so more
  of what it takes in radiates); water 0.32 (+12.0, wading with gravel); mud 0.20 (+5.9, under gravel: the squelch is
  the mud's air and water pushed out). The top and bottom of walking's grounds (carpet aside) 25 LU apart before, 15
  now. A landing on gravel now meets the voice's soft limiter (+11.6 dB over walking's peak, not +15.4).

## Decision

KEEP: a character's steps are heard as its feet move, on what they land on, without animation events or recordings.

## Rejected / not done

* Animation events (the clip's frames): a blend, a placed foot, a turn on the spot are not in any clip.
* Recordings by a floor tag: a project turns `sound_enabled` off and plays its own on the `footstep` signal.
* Clothing and gear (a jacket's rustle, a sword's clink), breathing; the character's weight shifting while standing.
* Snow's squeak in the cold, ice's creak, wet hard floors; a ground's material per texel of a mesh (a ray's collider
  is one ground, a terrain its layers).
