# 0191 — A face that speaks and blinks: the mouth from the voice, analysed as it plays

Status: **KEEP** (a new node, `FellowFaceAnimator`; without it nothing changes) · Branch `fellow/geometry` · Code:
`modules/fellow_animation/fellow_face_animator.{h,cpp}` · Checks: `benchmarks/project/bench/face_check.gd`
(headless: the analysis, a higher voice, the level, a minute's blinks; `--listen=1` under Movie Maker: the face
following a voice as it plays; `--many=N`), the regression scene `face`, the editor check (its icon).

## Problem

The readiness assessment's characters row: "no facial animation". A character speaking a line with a still face is
the first thing a player notices in a cutscene or a conversation; AAA engines move the mouth from the dialogue — from
phonemes aligned offline, or from the audio itself — and blink the eyes.

## Change

`FellowFaceAnimator` (a `Node`): `mesh_path` (the face's `MeshInstance3D`), `voice_path` (the player of its voice:
`AudioStreamPlayer`, 2D or 3D), `blend_shapes` (each channel's blend shapes; ARKit's names by default — `jawOpen`,
`mouthStretchLeft/Right`, `mouthPucker`, `mouthFunnel`, `mouthRollLower`, `eyeBlinkLeft/Right` — which many
characters' faces carry).

* **The voice analysed as it plays**: the stream's playback is mixed offline at the mix rate, a quarter of a second
  ahead of where it is read (the cost spread over the frames it plays in; a voice analysed once and shared by every face
  playing it, 256 kept), through five band-passes (two biquads each): 200-500 Hz, 500-900, 900-1500, 1500-3000 (the
  vowels' first two formants) and 4-8 kHz (the hiss of s, f, sh). Every 10 ms, from the last 20 ms: the level, and
  * the jaw open: the energy at 500-1500 Hz against 200-500 Hz (an open vowel's high first formant), at least 0.2 of
    the level;
  * the mouth wide: 1500-3000 Hz against 900-1500 Hz (a front vowel's high second formant and the valley under it);
  * the mouth round: below 900 Hz against 900-3000 Hz (a back vowel's low second formant), when not wide;
  * the lower lip under the teeth: 4-8 kHz against the rest;
  * the lips closed: as the level falls to `level_floor_db` (−45 dB; full at `level_full_db`, −18).
* **The face**: the channels read `anticipation` (40 ms) ahead of what is heard (the player's position, the time since
  the last mix, minus the output's latency; under Movie Maker the position), eased (`attack` 40 ms, `release` 80 ms),
  times `strength`, set on the blend shapes (a shape moved by several channels: their sum, at most 1; written only when
  it changed). A timeline can drive it instead: `voice_stream` read at `voice_time` (an animation track sets it).
* **Blinks**: every 2-6 s at random (`seed`), 0.18 s (closing in a third, closed a sixth, opening in half); `blink()`.
* `get_channel()`, `get_channels()`, `sample_voice(stream, time)` (the analysis there, with its scores),
  `get_stats()`, `get_global_stats()`, `clear_analysis_cache()`.

## Checks (cloud: the CPU, lavapipe)

`bench/face_check.gd`; the voice made by formant synthesis (a 120 Hz pulse train falling 12 dB an octave, three
resonators at a vowel's formants, the lips' radiation): /a/, /i/, /u/, /e/, /o/, an "s" (noise over 4 kHz), silences.

| lane | expected | result |
|---|---|---|
| the analysis in each sound's middle | /a/ the jaw, /i/ wide, /u/ round, "s" the teeth, silence closed; /e/ wide, /o/ more round than wide | /a/ jaw 1.0 (wide, round 0); /i/ wide 1.0 (jaw 0.2); /u/ round 1.0 (jaw 0.2); "s" teeth 1.0; silence closed 1.0, the rest 0; /e/ wide 1.0, jaw 0.55; /o/ jaw 0.97, round 0.79 |
| a higher voice (210 Hz) | the same | /a/ jaw, /i/ wide, /u/ round (Python's model of the analysis: the same from 100 to 250 Hz, /o/ round at 250) |
| the same /a/ at −6, −20, −35, −50 dB | the jaw less as quieter, none under the floor | 1.0, 0.68, 0.13, 0 |
| blinks over a minute | one every 2-6 s | 14, 2.25-5.65 s apart |
| the voice played (Movie Maker), the face following | the mouth with the sound, the blend shapes its channels | the jaw half open at /a/'s first sample (40 ms ahead, less the easing: 0 ms); in each vowel's middle its channel at 0.997; the `jawOpen` shape within 0.0001 of the channel |

The release template: the same results. Lavapipe: the faces filmed (open, wide, round in /a/, /i/, /u/); the
regression scene `face` (four faces on timelines: /a/, /i/, /u/, a blink caught closed) the same picture on every run
and from the release template.

Costs (the editor build, the cloud CPU): a face's frame 0.24 µs (100 faces on timelines 0.024 ms a frame); the
analysis 14 ms a second of voice, about 0.23 ms a frame while a voice plays (shared by the faces playing it).

How it was found to work: the first scores (the bands' energy shares, and "wide" against the strongest lower band)
could not tell /i/ from /u/ in the first synthesised vowels: without the lips' radiation (+6 dB an octave) their second
formant was 40 dB under the first; with it (as a recorded voice has it), /i/'s 1500-3000 Hz band is 4 dB over the
900-1500 Hz valley and /u/'s 11 dB under, /a/'s 13 dB under — the scores were set from those. Analysing a whole
voice when it is first played cost 49 ms (a hitch); it is analysed as it plays.

## Decision

Keep: faces with ARKit-named blend shapes speak and blink with a node and the voice's audio, no phonemes or authoring.

## Rejected / not done

* **Phoneme alignment** (a recogniser's or a text's phonemes timed to the audio, offline) and per-language visemes:
  the mouth follows the sound's spectrum, not the words; a stylised mouth.
* **Analysis on a worker thread**: the incremental analysis on the main thread keeps the face the same every run; a
  worker would have to be waited for when behind.
* **Emotion, brows, gaze and head motion**, a jaw bone (a skeleton's): blend shapes only, the mouth and the eyes.
* **Live microphone input**: a stream played by a player only.

## Later

Milestone 89 (decision 0197): the eyes' gaze and saccades; this record's lanes are unchanged; the regression scene
`face` re-blessed (pupils, looking at a point).
