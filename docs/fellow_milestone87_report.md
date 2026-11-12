# FellowEngine — milestone 87 report (low-passes in mix snapshots)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0195-snapshot-lowpass.md`.

## 0. Result

* **`FellowMixSnapshot.bus_lowpass`**: a pause menu or a dive muffles the game — a low-pass faded in and out with the
  snapshot, turned off (the bus as it was) when no snapshot filters it.
* **Checked**: under Movie Maker, a 4 kHz tone 36.6 dB lower under a 500 Hz cutoff and back within 0.02 dB after it,
  the editor build and the release template the same; the twelve earlier lanes unchanged.

## 1. Development

`modules/fellow_world/fellow_audio_director.{h,cpp}`, `doc_classes/FellowMixSnapshot.xml`;
`benchmarks/project/bench/audio_director_check.gd` (one lane).

## 2. Verification

Decision 0195's table; the laptop (job f396, milestone 97): under Movie Maker as on the cloud (4 kHz −36.6 dB under the
low-pass, 300 Hz −2.7 dB).

## 3. Measurements

The director's step with the thirteen lanes' directors: 16 µs a frame (editor build), the filters' mixing the audio
server's.

## 4. Next

A mixer view in the editor; other effects in snapshots.
