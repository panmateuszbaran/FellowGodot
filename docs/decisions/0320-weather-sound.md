# 0320 — The weather's sound: rain, wind and thunder synthesised

Status: **KEEP** · Branch `fellow/geometry` · `modules/fellow_fluid/fellow_weather_sound.*` (new: a stereo synthesis
on the audio thread — an internal `AudioStream` and its playback —, the rain's, the wind's and the thunder's);
`FellowRain3D`, `FellowWind3D`, `FellowLightning3D`: `sound_enabled`, `sound_volume_db`, `sound_bus`,
`get_sound_stats`, a `debug_render_*`; `FellowWind3D.sound_whistle`, `sound_leaves`; the bolt's channels built by one
function for the picture and the thunder · Measured with `benchmarks/tools/world_sound/` (new: `weather_sound.gd`
offline, `weather_sound.py`; `storm_demo.gd` with Movie Maker).

Checks: the cloud's CPU (the syntheses offline, headless; the storm with Movie Maker on lavapipe, the CPU's Vulkan);
no GPU. Nothing was listened to here: the physics and the levels are checked on spectra; how it sounds is the owner's
ear (he asked for more of the world's sounds after decision 0319's water).

## Problem

`FellowRain3D`, `FellowWind3D` and `FellowLightning3D` drew a storm in silence: a game had to find rain, wind and
thunder recordings, loop and crossfade them by the weather's strength, place the thunder by hand after the flash
(`struck` gave its distance) — and the loops do not know a roof over the player, the sea under the rain, how hard the
gust blowing where the player stands is, or the shape of the bolt.

## Change

Each node synthesises its sound, heard where the listener is (the viewport's `AudioListener3D`, else its camera), on
the audio thread (a stereo synthesis behind an internal `AudioStream`; the node posts its parameters under a lock), with
no samples (Andy Farnell's *Designing Sound* for the approach; the physics below):

* **Rain** (`FellowRain3D`): the drops farther off are a bed of pink noise without its lows, a hump about 2.5 kHz, its
  highs lost on the way; the near ones (0.4-4 m, 320 a second at full rain) each its own, sized from Marshall and
  Palmer's distribution for the rain's rate — on water a tick and, for drops of 0.8-1.1 mm, the small bubble they
  entrain ringing at 13-21 kHz (Pumphrey and Crum's "type I"; larger drops sometimes a lower bubble; decision 0319's
  Minnaert model), the far ones a fizz above 9 kHz; on the ground a click through a band of the surface. Under a cover
  the drops knock on it (noise through a resonance of 300-1400 Hz, 450 a second) over a low drumming, and the rain
  outside is muffled (low-passed to 900 Hz). What is around the listener is probed every 0.2 s: five rays up (the share
  covered, the cover's height), eight down four metres round (the share falling on a `FellowWater3D` before the
  ground). Silent while it snows.
* **Wind** (`FellowWind3D`, the active one): the speed where the listener is, with its gust (`get_velocity_at`), eased
  over 0.15 s: a low rumble (a two-pole low-pass of pink noise, 70 + 6 v Hz) and a rush (a band about 220 + 45 v Hz)
  swelling a few times a second; edges whistling from 5 m/s (Aeolian tones: narrow bands at the Strouhal frequency
  0.2 v / d for edges of 3, 7 and 15 mm, each wandering ±15 %, `sound_whistle`); leaves rustling in grains of high
  noise (`sound_leaves`).
* **Thunder** (`FellowLightning3D`): Ribner and Roy's quasilinear model of thunder from a tortuous channel. The bolt's
  channel as drawn (its segments, branches weaker) and its unseen run through the cloud (3-6 km of a wandering walk at
  the cloud's height: most flashes to the ground have one) heard from the listener's place at the strike: every
  segment's pressure pulse — an N-wave of 6-12 ms (branches 2.5-5.5 ms), lengthening with distance — arrives after its
  distance / 343 s; broadside to the listener a segment's pulse arrives at once (a clap), along the line of sight it is
  drawn out and weaker; each segment's small kinks add short pulses (0.6-2 ms: the crackle of a near strike); the air
  takes the highs over each segment's distance (a pole at 14 kHz / (1 + d / 250 m)); every stroke of the flash again.
  Built on a worker thread at the strike, played from its first arrival with an outdoor reverberation (four delay
  lines of 80-200 ms, damped), from the strike's side.
* The bolt's channels are now made by one function (the picture's, the same random draws: the regression suite's
  lightning scene identical) that the thunder test calls without drawing.

## Measurements

`weather_sound.gd` + `weather_sound.py` (48 kHz stereo, offline):

| rain | loudness | spectral centroid | above 9 kHz | below 500 Hz | drops heard a second | channels' correlation |
|---|---:|---:|---:|---:|---:|---:|
| on the ground, 0.2 | −38.0 dBFS | 6 390 Hz | 25 % | 8 % | 9 | 0.25 |
| on the ground, 0.5 | −32.6 | 5 250 | 19 % | 10 % | 4 | 0.16 |
| on the ground, 1.0 | −28.0 | 5 170 | 18 % | 11 % | 1 | 0.13 |
| on water, 0.5 | −31.2 | 9 900 | **50 %** | 9 % | 0 | 0.04 |
| under a roof 2.5 m up, 0.5 | −27.9 | **1 130** | 1 % | **28 %** | 3 | 0.42 |

(Heavier rain is louder and its near drops lost in the roar; on water the small bubbles' band is half the sound; under
a roof the sound drops to the knocks and drumming. Drops heard: 1 ms windows over four times the median.)

| wind | loudness | centroid | edges whistling (predicted 0.2 v / d → found, over the spectrum around) |
|---|---:|---:|---|
| 3 m/s | −45.0 dBFS | 595 Hz | (none: under 5 m/s) |
| 8 m/s | −31.8 | 683 | 3 mm 533 → 520 Hz (+4 dB), 7 mm 229 → 238 (+7) |
| 14 m/s | −24.0 | 474 | 933 → 898 (+4), 400 → 438 (+5), 187 → 202 (+7) |
| 20 m/s | −19.0 | 409 | 1 333 → 1 282 (+5), 571 → 598 (+6), 267 → 202 (+2) |

A gust 6 → 16 → 6 m/s: −38 → −23 → −40 dBFS, following it within 0.5 s.

| thunder (strike to the right) | heard at (distance / 343) | rolls (to −30 dB) | peak | crack's centroid (first 0.25 s) | all | right − left |
|---|---:|---:|---:|---:|---:|---:|
| 300 m | 0.898 s (0.875) | 5.7 s | −11.3 dBFS | 244 Hz | 170 Hz | +6.3 dB |
| 800 m | 2.356 (2.332) | 6.4 | −14.9 | 171 | 125 | +5.9 |
| 800 m, ahead | 2.362 (2.332) | 6.3 | −13.0 | 162 | 169 | +0.1 |
| 1 500 m | 4.396 (4.373) | 4.2 | −16.9 | 158 | 80 | +6.5 |
| 3 000 m | 7.620 (8.746) | 3.5 | −19.8 | 197 | 58 | +6.8 |

Heard 23 ms after the ground point's distance / 343 s (the nearest segment a few metres up); the 3 km strike's cloud
channel ran towards the listener and was heard first, 1.1 s earlier. Nearer: louder, brighter, a crackle at its
start; farther: deeper and duller.

**The CPU** (the audio thread, a second of sound): rain 1.7-2.4 ms (4.0 ms under a roof: its knocks ring), wind 1.6-1.8
ms, thunder 1.7-2.1 ms (its reverberation running), the build at a strike on a worker thread.

**The storm** (`storm_demo.gd`, Movie Maker, 16 s: a pier over a rough sea, rain 0.8, wind 13 m/s gusting, strikes at 2
km, 600 m and 1.2 km, the camera walking along the pier in the open, under its roof and out; six crates in the water):
the rain probed 75 % water around the pier, 2 216 drops near and 3 109 knocks on the roof while under it; the thunder
built 3 times; the crates' laps and splashes in the water's voices (572); peak −1.2 dBFS for everything together.

**Unchanged:** the regression suite's lightning, rain, snow and water scenes identical to their goldens (the bolt's
channels the same).

## How it got there

* The first rain was a hiss: a centroid of 7.2 kHz, no drop heard in it; a roof was a 126 Hz rumble (95 % under 500
  Hz). The bed's highs taken away (7 kHz), its level down 4 dB, the near drops twice as loud, the roof's drumming a
  quarter: drops heard, the roof's knocks at 1.1 kHz.
* The first wind was all rumble (a centroid of 50 Hz, a microphone in the wind rather than the wind); its whistles
  stood 3 dB over the rush, 10-25 % flat (their wander drifted to one side and stayed): the rumble a quarter, the rush
  louder, the whistles three times as loud and their wander pulled back to the middle.
* The first thunder rolled 2-3 s whatever its distance and peaked at −29 to −35 dBFS with no crack (a centroid of 30-80
  Hz): the drawn channel alone spreads its arrivals by under a second. The channel through the cloud (long rolls), the
  small kinks' short pulses (the crack), a longer reverberation and 15 dB more.
* Then the levels set against each other: rain 9 dB up (heavy rain in the open about −28 dBFS), wind 6 dB up (20 m/s
  about −19 dBFS); the storm peaked at −0.9 dBFS with everything at once: the thunder 3 dB down (−1.2).

## Decision

KEEP: a storm is heard without a recording, as the weather is where the listener stands.

## Rejected / not done

* Recordings crossfaded by strength: what games usually do; a project can turn `sound_enabled` off and play its own.
* The rain on other surfaces (leaves, metal, glass): the ground's and a roof's resonances only; per-material drops
  need the surface's material under the listener.
* Wind against the scene's geometry (corners, gaps): the edges' whistles are generic.
* Thunder from the cloud's other flashes (intracloud lightning) and over a large map's terrain (its echoes modelled by
  a reverberation, not the land).
* Snow (silent here), hail, sleet.
