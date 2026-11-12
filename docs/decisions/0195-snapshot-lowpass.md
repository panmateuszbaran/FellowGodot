# 0195 — A mix snapshot's low-pass: the game muffled under a menu

Status: **KEEP** (on `FellowMixSnapshot`: `bus_lowpass`, empty by default) · Branch `fellow/geometry` · Code:
`modules/fellow_world/fellow_audio_director.{h,cpp}` · Checks: `benchmarks/project/bench/audio_director_check.gd`
(thirteen lanes under Movie Maker: the twelve of decisions 0190 and 0193 unchanged, one new).

## Problem

Decision 0190's snapshots changed the buses' levels only; its "Rejected" named effects in snapshots. A pause menu
muffles the game behind it, a character under water hears it dull, a near explosion deafens: a low-pass on the
game's buses, faded in and out with the state.

## Change

* `FellowMixSnapshot.bus_lowpass`: a bus's name and a cutoff in Hz. While a snapshot with it is in, the director puts
  an `AudioEffectLowPassFilter` of its own at the end of that bus (made once, kept), its cutoff moved from 20.5 kHz to
  the snapshot's along the snapshot's weight — evenly in octaves, so the fade sounds even — the lowest of the active
  snapshots' cutoffs winning; when none filters the bus the effect is turned off (the bus mixed as without it), and the
  director takes its effects away when it leaves the tree. The configuration warning names a bus missing from the
  layout in `bus_lowpass` as in `bus_volumes`.

## Checks (cloud: the CPU, lavapipe)

`bench/audio_director_check.gd` under Movie Maker, the editor build and the release template the same: a loop of a
300 Hz and a 4 kHz tone on the bus "Muffle"; a snapshot filtering it at 500 Hz (0.2 s in and out) pushed for 1.5 s;
the master bus measured at both tones (Goertzel):

| | 300 Hz | 4 kHz | the bus's effects |
|---|---|---|---|
| before | −24.06 dB | −24.09 dB | none |
| under the snapshot | −26.73 dB (−2.67: the filter's slope under its cutoff) | −60.72 dB (−36.63: 12 dB an octave over three octaves) | one low-pass, on, at 500.0 Hz |
| after (popped) | −24.06 dB | −24.07 dB | the low-pass off |

The other twelve lanes: the same as before (the run 4.3 s longer, the duck's music stopped before the new lane).

## Decision

Keep: snapshots muffle buses as well as lower them.

## Rejected / not done

* **Other effects** (reverb sends, high-passes, distortion) in snapshots: the low-pass is the common one; others are
  the bus's own, set by the game.
* **Removing the effect when unused**: turned off, it costs nothing, and the bus's effect indices stay as they were
  while the director lives.
