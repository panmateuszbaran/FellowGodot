# 0193 — Virtual voices: sounds that go on unheard and come back where they would be

Status: **KEEP** (on `FellowSoundEvent`: `virtual_voice` and `steal_oldest`, off by default) · Branch
`fellow/geometry` · Code: `modules/fellow_world/fellow_audio_director.{h,cpp}` · Checks:
`benchmarks/project/bench/audio_director_check.gd` (twelve lanes under Movie Maker: decision 0190's eight unchanged,
four new; `--many=1 --virtual=1`).

## Problem

Decision 0190's director refused a sound it had no voice for, stopped one that gave its voice up and did not start one
beyond its distance; its "Rejected" named virtual voices and stealing an event's oldest instance. A machine's hum, a
river or an engine that loses its voice to a gunfight must come back when the voices free — where it would be by then,
not from its start — and one the listener walks towards from far away must be heard when it comes within reach.

## Change

* **`FellowSoundEvent.virtual_voice`**: such a sound is kept as a **virtual voice** — its stream, level, pitch, place
  (following its node) and the director's clock at its start, no player, nothing mixed — instead of being refused (no
  voice for it), given up (another takes its voice: faded out in `steal_fade`, then virtual) or not started (beyond
  `max_distance`); a playing one carried 5 % beyond its distance goes virtual too. Each frame a virtual voice within
  reach takes a free voice and resumes at the position it would have reached (its time times its pitch; a WAV loop
  wrapped between its loop points, another stream's loop round its length); a one-shot whose time is over ends
  (`voice_finished`), unheard. `is_virtual(voice)`; `is_playing` is true for it; `get_voices()` marks it; `get_stats()`
  counts `virtual`, `virtualized`, `realized`. Virtual voices do not trigger a snapshot.
* **`FellowSoundEvent.steal_oldest`**: over `max_instances`, the event's oldest voice gives way (faded out) instead of
  the new one being refused.
* The director keeps the number of its voices with players as voices change (it was counted over all voices for each
  play and, for virtual voices, each frame: 2.7 ms a frame with 4 400 virtual voices; 0.06 ms now).

## Checks (cloud: the CPU, lavapipe)

`bench/audio_director_check.gd` under Movie Maker; the editor build and the release template the same:

| lane | expected | result |
|---|---|---|
| 2 voices: a quiet loop (virtual) at 10 m from 0.17 s, two important loops at 5 m from 0.67 s, stopped at 1.67 s | it loses its voice, plays on unheard, is resumed where it would be | virtual at 1.0 s (2 voices playing), resumed by 1.72 s at 0.551 s into its 1 s loop (0.550 expected); virtualized once, realized once |
| a loop that carries 30 m on a node coming from 50 m to 10 m, then taken to 60 m | virtual far, heard within reach, virtual again | virtual at 46.8 and 31.8 m, heard at 25.2 and 10.4 m, virtual at 60.1 m; never culled |
| a 0.5 s sound started 40 m away (it carries 20 m) | ends unheard | virtual and playing 0.17 s after its start; ended 0.48 s after it (`voice_finished`), never given a player |
| 2 instances at most, the oldest giving way; played three times | the first stops, the last two play | the first stopped (stolen), the two others playing, none refused |

Decision 0190's eight lanes: the same results as before. `--many=1` (headless): 64 voices and 50 requests a frame
— 16 µs a frame for the director as before; with `--virtual=1` (the requests beyond reach or without a voice kept)
4 400 virtual voices after 4 s: 62 µs a frame, 1.2 µs a request.

## Decision

Keep, off by default: loops and long sounds come back where they would be; a game marks the events that should.

## Rejected / not done

* **Virtual voices competing by priority** for voices (taking a quieter real voice's): a virtual voice takes a free
  voice only, so a voice is not handed back and forth each frame.
* **Resuming streams that cannot seek** (a generator, a microphone): resumed from where the player puts them.
