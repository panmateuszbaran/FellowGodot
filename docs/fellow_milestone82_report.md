# FellowEngine — milestone 82 report (sound events, voices and mix snapshots)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0190-audio-director.md`.

## 0. Result

* **`FellowSoundEvent`**: a sound as a game asks for it — variations picked without repeats (or in turn), its level
  and pitch varied, how many at once, how soon again, how far it carries, how important it is, its bus.
* **`FellowAudioDirector`**: plays events from a pool of players within a budget of voices; when they are all taken,
  the least important and quietest at the listener gives its voice to a more audible new sound (faded out in 30 ms);
  voices follow nodes.
* **`FellowMixSnapshot`**: mix states fading the buses' levels, pushed by the game or while a voice plays on a
  trigger bus — the music ducked under dialogue by exactly the snapshot's 12 dB, measured on the master bus.
* **Checked**: eight lanes under Movie Maker on the editor build and the release template (the same results), a
  stress of 64 voices, the editor check (the classes' icons).

## 1. Development

* `modules/fellow_world/fellow_audio_director.{h,cpp}` (the three classes), `register_types.cpp`, `config.py`,
  `doc_classes/FellowAudioDirector.xml`, `FellowSoundEvent.xml`, `FellowMixSnapshot.xml`, `icons/`.
* `benchmarks/project/bench/audio_director_check.gd`; the editor check's icon list.

## 2. Verification

* The lanes (decision 0190's table) under Movie Maker on lavapipe: the audio mixed frame by frame, the same every run;
  the release template the same.
* The editor check passes (33 icons).
* Decision 0186's acoustics and 0187's vehicle sounds are untouched (the director plays through stock players, which
  the acoustics node hears like any other).
* The laptop (job f396, milestone 97): the lanes (fourteen after milestone 96) under Movie Maker as on the cloud, lane
  for lane (the duck −12.00 dB); 64 voices 7.3 µs a frame, a request 0.19 µs.

## 3. Measurements

The director's process step: 12-16 µs a frame with 64 voices (following, levels at the listener, ended voices);
0.2-0.3 µs a request at 50 requests a frame, most refused or beyond their distance (editor build, cloud CPU).

## 4. Next

Virtual voices (loops resumed where they would be when a voice frees), effects in snapshots (a low-pass under a
pause menu), stealing an event's oldest instance, and a mixer view in the editor.
