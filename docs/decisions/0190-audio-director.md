# 0190 — Sounds as a game asks for them: events, a budget of voices, mix snapshots

Status: **KEEP** (new classes: `FellowAudioDirector`, `FellowSoundEvent`, `FellowMixSnapshot`; without a director in
the scene nothing changes) · Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_audio_director.{h,cpp}` ·
Checks: `benchmarks/project/bench/audio_director_check.gd` (eight lanes under Movie Maker; `--many=1`), the editor
check (the classes' icons).

## Problem

The readiness assessment's audio row: "no Wwise/FMOD-class middleware (authoring, banks, mixing states)". A game plays
a footstep, a gunshot or a line of dialogue as an event — one of several recordings, its level and pitch varied so it
does not repeat, not more than a few at once, not again too soon, not at all beyond its distance — within a budget of
voices where the least important and quietest gives way; and its mix changes with the game's state (a pause menu, the
music lowered while someone speaks). Stock Godot plays a stream in a player and leaves all of that to the game's code
(`AudioStreamRandomizer` picks a variation and varies the pitch, `max_polyphony` limits one player's own voices).

## Change

* **`FellowSoundEvent`** (a resource): `streams` (the variations), `selection` (at random without the last again,
  at random, in turn), `volume_db` and `volume_random_db` (± uniform), `pitch_scale` and `pitch_random_semitones`,
  `max_instances`, `cooldown`, `priority` (0-255), `bus`, `spatial`, `unit_size`, `max_distance`,
  `attenuation_model`; `get_audibility_db(distance)` (its level at the listener, the attenuation as
  `AudioStreamPlayer3D`'s, at most +3 dB).
* **`FellowAudioDirector`** (a node): `play(event, position)`, `play_on(event, node)` (following it; where it was when
  the node is freed) return a voice's id, or 0 when the event is over its instances, in its cooldown or beyond its
  distance from the listener (the `AudioListener3D`, the camera or `listener_path`). The players are a pool of its own
  (internal children, made as needed, reused). `max_voices` (32) play at once: when they are all taken the new sound
  takes the voice of the least important, quietest one now (its event's priority, then its level at the listener,
  then the oldest), faded out in `steal_fade` (30 ms), when it is more important, or as important and louder;
  otherwise it is not played (`steal_voices` off: never taken). Each frame (the process step) it follows the nodes,
  measures the voices' levels at the listener and lets go of the ended ones (`voice_finished(voice, stolen)`).
  `stop`, `stop_event`, `stop_all` (with a fade), `is_playing`, `get_voice_count`, `get_event_voice_count`,
  `get_voice_player`, `get_voices`, `get_stats`; `seed` (the same picks every run).
* **`FellowMixSnapshot`** (a resource): `bus_volumes` (dB added to buses), `fade_in`, `fade_out`, `trigger_bus`. The
  director's `push_snapshot` / `pop_snapshot`, and its `snapshots` with a trigger bus: active while one of its voices
  plays on that bus (a duck). A snapshot's weight goes to 1 over its fade in and back to 0 over its fade out; each bus
  is set to its level before the snapshots plus the active snapshots' changes times their weights (a fade linear in
  dB), and given back its level when none changes it (and when the director leaves the tree).

## Checks (cloud: the CPU, lavapipe)

`bench/audio_director_check.gd` under Movie Maker (the audio mixed frame by frame: the same result every run), the
sounds tones made by the script, the listener a camera at the origin; each lane its own director:

| lane | expected | result |
|---|---|---|
| four variations, 40 plays, ±3 dB, ±2 semitones; a sequence | none twice running, all used, within the ranges; 0 1 2 3 0 1 2 3 | no repeat, all four, −8.76 to −3.14 dB (−6 ± 3), pitch 0.892-1.114 (±2 semitones: 0.891-1.122); the sequence in turn |
| at most 3 instances, 5 asked for at once | 3 play, 2 refused | the same |
| a 0.1 s cooldown, asked for every frame for a second | 10 starts | 10, 50 refused |
| carries 50 m: at 30 and at 80 m | the near one plays | the far one not started |
| 8 voices: 8 quiet loops 20-27 m away (priority 64), 4 important ones (200), 4 quieter (40 m), a loud one (2 m) | the 4 farthest give way to the important; the quieter refused; the loud takes the quietest's voice | the important 4, the loud one and the 3 nearest left; 5 voices taken (5 `voice_finished` with `stolen`), 4 refused, never more than 8 |
| a snapshot (−20 dB on a bus at −3 dB, 0.5 s in and out) pushed at 1 s, popped at 2 s | −13 half way, −23, −13, back to −3 exactly | −13.0 at 1.25 s, −23.0 from 1.5 s, −13.0 at 2.25 s, −3.0 from 2.5 s |
| a duck (−12 dB on the music in 0.05 s, out in 0.4 s) while a 0.6 s line plays on "Dialogue"; the master bus captured, the music's 220 Hz measured | 12 dB lower under the line, as before after it | −21.04 dB before, −33.04 under the line (−12.00), −21.04 after; the bus at −15 within 3 frames, back 0.4 s after the line |
| a voice following a node at 5 m/s, the node freed | at the node every frame; on where it was | 0 m off; it plays on 0 m from where the node was |

The release template: the same results. The director's process step: 9 µs a frame for the eight lanes' directors
(editor build). `--many=1` (headless): 64 voices, 50 requests a frame at random places within 100 m (eight events,
their priorities 64-176, carrying 80 m): 12-16 µs a frame for the director's step with 64 voices, 0.2-0.3 µs a
request (most refused or beyond their distance).

How it was found to work: the first follow lane measured the sound 8 cm (a frame's motion) from where the node was
freed — the node had moved in the physics step it was freed in, and a node freed with `queue_free()` in the physics
step is gone before the process step; the check now frees it before it moves. Quitting headless at once after
stopping 64 voices left their playbacks in the audio server (stock: they are let go by its mixing thread); the check
waits a quarter of a second.

## Decision

Keep: events, a voice budget with priorities and audibility, and snapshots with ducking, from a node and two
resources, without middleware.

## Rejected / not done

* **Virtual voices** (a loop that lost its voice resumed where it would be when one frees): a loop given up stops.
* **Stealing the oldest instance** of an event over its limit (common for footsteps): the new one is refused.
* **Authoring tools** (banks, a mixer window, live connection to a running game) and **effects in snapshots** (a
  low-pass on a bus under a pause menu): the snapshot changes levels only.
* **A ducked level that follows the speech's loudness** (a compressor's side chain): stock's `AudioEffectCompressor`
  has one; the duck here follows whether a line plays.

## Later

Milestone 85 (decision 0193): virtual voices and the oldest instance giving way, both off by default; this record's
eight lanes are unchanged.
