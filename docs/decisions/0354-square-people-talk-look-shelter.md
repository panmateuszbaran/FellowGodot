# 0354 — The square's people talk, look and point, leave in the rain, warm their hands

Status: **KEEP** · Branch `fellow/geometry` · Code: `showcase/scripts/rooftops_crowd.gd` (the social layer: looking,
pointing, talking, the weather's say), `showcase/scripts/rooftops_bubbles.gd` (new: speech bubbles);
`showcase/tools/blender/mannequin_anims.py` (`clip_wave`, `clip_point_up`, `clip_warm_hands`, `clip_huddle`),
`rooftops/character/mannequin_clips.res`, `tools/build_rooftops.gd` (`LOOPS`); `tools/climb_look.gd`
(`--program=talk`, `--program=lookup`, `--crowd=1`).

## Problem

The owner: "and some interactions with the NPCs?" Since milestone 208 the square's people could be run into; nothing
else they did knew the player was there, and the weather (milestone 211) passed them by.

## Change

* **They look at the player**: each person's head turned by a `LookAtModifier3D` after its clip (the player's own
  settings: 120° across, 80° up and down, eased over 0.35 s) at the player's eyes — the ten nearest of those within 7 m,
  and, while the player is up a wall or on a roof (over 2.5 m), everyone within 24 m; the weight eased in and out over
  0.5 s.
* **Pointing up**: while the player is up there, every 6 s the nearest of the standing people or walkers (3-24 m away,
  none pointing in the last 30 s) stops, turns to the player and plays "point_up" (the arm raised and jabbed twice, the
  head and chest lifted), shouting ("Guarda! Up there!", "Madonna, he'll break his neck!", ...).
* **Talking** (E, the interact action, within 2.4 m and in front of the player, on the ground): they stop (a walker's
  way waits), turn to the player, wave ("wave") and talk ("talk"); a speech bubble over their head says their lines —
  a greeting (or "You! You knocked me flat!" to the one who did), the player's climbing if seen in the last 90 s, the
  weather (a storm, the rain, the snow, grey skies, the golden evening), a word on the town (the bell tower, the
  market, the fountain, the brazier's keeper) — E for the next, each held 7 s at most; then a farewell and back to what
  they were doing. The one in reach is marked "E  Talk".
* **Speech bubbles** (`rooftops_bubbles.gd`): over the speaker's head on the screen, smaller with the distance, hidden
  behind the camera or beyond 28 m, fading in and out, the nearer drawn over the further; their font and size follow
  the window's height.
* **The weather**: in the rain (its intensity over 0.3) the standing groups leave the square by the nearest street and
  are gone once out of sight; they come back when it stops (under 0.12), one by one; the walkers hurry (1.45 times their
  pace); the stall keepers huddle under their awnings ("huddle"). Three people stand round the brazier; in the cold
  (under 5 °C: the snow, the blizzard) they warm their hands at it ("warm_hands").
* **Run into** (milestone 208), they shout: "Ehi!", "Attento!" staggered; "Ow... my back.", "Maleducato!" getting up.
* Four new clips made in Blender as the others (`mannequin_anims.py`): "wave" 2.4 s, "point_up" 3.6 s, "warm_hands"
  6 s (looping: the hands held out to the fire, rubbed, held out again), "huddle" 5 s (looping: the arms crossed, the
  shoulders up, looking out at the weather now and then).

## Measurements (headless, the whole scene)

| | |
|---|---|
| `--program=talk` (walking up to one of the middle's group, E at 1.4 s then every 2.6 s) | talking from 1.4 s, four lines, a farewell at 9.2 s; 5-7 of the people looking at the player |
| `--program=lookup` (the demo's climb up CasaCrema) | looking: 4 on the ground, 10 once the player is 3 m up; one pointing every 6 s (3 in 18 s) |
| `--program=weather --crowd=1` (fair, rain from 1 s over 4 s, fair at 25 s, snow at 40 s) | 15 of the 15 standing people (not the keepers) leaving within 8 s of the rain at 0.6; out of sight 12-24 s later; all back in their places 29 s after it stopped; the three at the brazier warming their hands in the snow |
| the people | 49 (46 + 3 at the brazier) |

* The demo's path unchanged (the whole demo, 60 s, against milestone 211's).

## Decision

KEEP: the square's people look, point, talk, shelter and warm themselves.

## Rejected / not done

* Sheltering under the stalls' awnings: they cover the stall and its keeper only. The groups leave by the streets;
  milestone 213's osteria takes them in.
* The seated people standing up to leave in the rain: no clip to stand up from the bench (they would slide out of
  their sitting pose); they stay.
* A dialogue tree with answers: the owner asked for interactions; a line at a time, E for the next.
