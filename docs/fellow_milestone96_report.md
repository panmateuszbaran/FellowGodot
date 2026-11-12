# FellowEngine — milestone 96 report (voice limits by bus)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0204-bus-voice-limits.md`.

## 0. Result

* **Voice limits by bus** (`FellowAudioDirector.bus_voice_limits`): dialogue, music and effects keep their own budgets
  of voices — a burst of effects cannot take the dialogue's.
* **Checked**: one lane under Movie Maker on the editor build and the release template (the same), the thirteen
  earlier lanes unchanged.

## 1. Development

`modules/fellow_world/fellow_audio_director.{h,cpp}`, `doc_classes/FellowAudioDirector.xml`;
`benchmarks/project/bench/audio_director_check.gd` (one lane).

## 2. Verification

Decision 0204's table. The laptop: not run (not linked to the session).

## 3. Measurements

A play on a limited bus counts that bus's voices (a pass over the voices).

## 4. Next

Limits by group of events.
