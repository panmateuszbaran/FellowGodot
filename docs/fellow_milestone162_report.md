# FellowEngine — milestone 162 report (the reference's ground, tiles and feet)

Status: **FINAL** (2026-11-20). Branch `fellow/geometry`. Everything ran in the cloud (CPU, lavapipe); the laptop's GPU
stays paused at the owner's request.

## 0. Result

The owner: "I don't like the look of the tiles and the ground in the first scene, match them more to the reference —
and use these shots for the feet too." Decision 0303:

* **The ground**: the square and the reference's street laid with oblong setts in running courses, as the video's.
  They are Poly Haven's `square_cobblestone` (CC0, fetched as `setts`): warm, their courses straight, their grain
  softened. The travertine bands are gone, and the setts run up to the reference house's wall.
* **The tiles**, on every roof: wide, low rounded covers in columns, a dark valley between them, each course's end a
  thick rounded step, each tile one of three firings. Their geometry is halved where nothing shows. The reference
  house's roof has no chimney.
* **The feet**: a slim slipper of a foot, 9 cm across with the forefoot 6 cm high (11 and 8.5 before). The ankle's ball
  is smaller and half sunk in it, and the shin is carried down to it.

## 1. Checks (cloud)

* The ground (frames 1, 25), the tiles (84, 88, 93) and the feet (1, 25, 45) cropped beside the video's.
* The whole shot beside the video: `reference162_sbs.mp4`.
* `anim_quality.py`: 38 of 38 gates, none worse than milestone 158's baseline.
* The laptop: `.tools\bundles\m162.bundle`. `Showcase - Dachy.bat` brings `.tools\wt-fellow` to this milestone and
  fetches the setts' texture; the engine (`m156e`) is unchanged.

## 2. Next

* The square round the video's house as a set for the shot, if the owner wants it.
* The game's own clips from the measured performance.
* The GPU, when the owner allows it.
