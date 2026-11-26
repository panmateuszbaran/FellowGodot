# 0364 — The walkers go home at night

Status: **KEEP** · Branch `fellow/geometry` · Code: `showcase/scripts/rooftops_crowd.gd` (`WALKERS_HOME`,
`_walkers_home()`, `_walker_rejoin()`, `_walker_detour()`), `showcase/scripts/rooftops_night.gd` (`--day_at=<s>`).

## Problem

Milestone 218's open point: at night the square's standing groups went home or into the osteria, but its walkers kept
their day's routes — the square at night as busy with people crossing it as at the golden hour.

## Change

* At night (milestone 218's `NIGHT_AWAY`), the walkers without a lantern — `WALKERS_HOME` (0.75) of them, chosen by a
  fixed sequence — go home: each walks round what stands in the square to the street nearest it (the square's exits)
  and down it, out of sight, with a word now and then ("Buonanotte!", "A domani!"). The lantern carriers (every third
  walker) stay on their ways.
* By day each comes back out of the street it went down, walks to the nearest point of its way and on along it (its
  lane eased in from where it is). A walker bumped while going or coming back walks on along its way from where it was
  caught.

## Measurements

Headless, the rooftops at fixed 60 fps, night at once and the day back at 14 s (`--night=1 --day_at=14`): the 63
people's acts each second.

| | at night (24 s) | the day back (+30 s) |
|---|---|---|
| milestone 218 (the groups only) | 14 away or leaving | all back |
| this milestone | 28 away or leaving (14 walkers) | all 63 back |

The RTX's frames of the square at night (f621): the lantern carriers, the stall keepers, the brazier's ring and the
osteria's; the picture in decision 0363. By day nothing changes (the demo's path as milestone 211's).

## Decision

KEEP.
