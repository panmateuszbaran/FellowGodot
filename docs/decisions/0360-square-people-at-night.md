# 0360 — The square's people at night

Status: **KEEP** · Branch `fellow/geometry` · Code: `showcase/scripts/rooftops_crowd.gd` (`NIGHT_AWAY`,
`LANTERN_EVERY`, `_light_lanterns()`, the night's lines).

## Problem

Milestone 216's night: the square's 63 people went on with the evening's routine under the moon — the groups chatting
in the square's middle, the walkers in the dark.

## Change

* **The groups go home or into the osteria** at night (rooftops_night.gd's `get_night()` over 0.6), as they do in the
  rain (milestones 212-213: by the streets, or into the osteria's free places when its door is nearer), and come back
  by day; the stall keepers stay. Going, they say "Buonanotte!", "Time for a glass, then bed." or "A domani!" (in the
  rain still "Quick, inside!").
* **Every third walker carries a lantern** in the right hand at night (a small iron lantern, its candle's warm light —
  1.2 of energy, 5 m, no shadows — swinging with the arm), put out by day: 8 lights.
* Their talk at night: "Late to be out, forestiero. The osteria is still open.", "Look at the moon over the tower!",
  "Mind the dark corners — and the watch."

## Measurements

The RTX, 1280x720, the square at night (job f614): of the 63 people, 11 leaving within a second, 13 gone after 20 s
(and 2 on their way) — the others are the walkers, the seated, the keepers and the osteria's; 8 lanterns lit. The
osteria's door at night: its places taken by the square's people. The demo's path unchanged (0.1 mm over 60 s: no
night in it).

## Decision

KEEP.
