# 0204 — Voices a bus may play

Status: **KEEP** (on `FellowAudioDirector`: `bus_voice_limits`, empty by default) · Branch `fellow/geometry` · Code:
`modules/fellow_world/fellow_audio_director.{h,cpp}` · Checks: `benchmarks/project/bench/audio_director_check.gd`
(fourteen lanes under Movie Maker: the thirteen earlier unchanged, one new).

## Problem

Decision 0190's budget was one for all sounds: a burst of footsteps or debris could take every voice from the
dialogue and the music. Mixes give buses their own budgets (dialogue 4, effects 24, ambience 8).

## Change

`bus_voice_limits` (a bus's name and a count): a new sound on a bus at its limit is handled as at the director's
`max_voices` (decision 0190) — it takes the voice of the bus's least important, quietest sound when it matters more
(that one fading out or going virtual, decision 0193), or it goes virtual or is refused — before the director's own
budget is looked at; a virtual voice is resumed only when its bus has room as well as the director.

## Checks (cloud: the CPU, lavapipe)

`bench/audio_director_check.gd` under Movie Maker, the editor build and the release template the same: a director of 8
voices, at most 2 on "Sfx"; three loops at 10, 12 and 14 m, then one at 5 m:

| | expected | result |
|---|---|---|
| after the three | 2 playing, the farthest refused | 2; the 14 m one refused |
| after the near one | it takes the quieter's voice | the 10 m and 5 m ones playing; 1 voice taken, 1 refused |

The thirteen earlier lanes: the same results.

## Decision

Keep: a mix's buses keep their voices.

## Rejected / not done

* **Limits by group of events** (all footsteps together across buses): by bus only.

## Follow-up (milestone 97)

On the laptop (job f396) 4 447 virtual voices cost 152 µs a frame, 62 at decision 0193: each virtual voice looked its
bus up in `bus_voice_limits` (a `Dictionary` keyed by a `Variant`) every frame, and counted the bus's voices when it
had a limit, before the cheap tests. Now the director's free voice and the reach are tested first and the bus only for
a voice that could be resumed: 204-224 → 57-73 µs a frame on the cloud (release template), the fourteen lanes
unchanged.
