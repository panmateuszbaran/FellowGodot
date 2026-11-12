# FellowEngine — milestone 85 report (virtual voices)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0193-virtual-voices.md`.

## 0. Result

* **Virtual voices** (`FellowSoundEvent.virtual_voice`): a loop or a long sound that finds no voice, gives its voice
  up to a more important one or is out of reach goes on unheard and comes back where it would be by then — within
  1 ms in the check — when it is within reach and a voice is free; a one-shot that ends meanwhile is let go.
* **The oldest instance gives way** (`steal_oldest`): footsteps and impacts over their limit replace the oldest.
* **Cheaper with many sounds**: the director's count of voices kept as they change (4 400 virtual voices 62 µs a
  frame instead of 2.7 ms).

## 1. Development

* `modules/fellow_world/fellow_audio_director.{h,cpp}`; `doc_classes/FellowAudioDirector.xml`,
  `FellowSoundEvent.xml`.
* `benchmarks/project/bench/audio_director_check.gd`: four lanes, `--virtual=1`.

## 2. Verification

* Decision 0193's table under Movie Maker, the editor build and the release template the same; decision 0190's eight
  lanes unchanged.
* The laptop: not run (not linked to the session).

## 3. Measurements

The director with 64 voices: 16 µs a frame (as before); with 4 400 virtual voices: 62 µs a frame, 1.2 µs a request.

## 4. Next

Effects in snapshots (a low-pass under a pause menu); virtual voices competing by priority.
