# FellowEngine — milestone 159 report (the reference's light)

Status: **FINAL** (2026-11-20). Branch `fellow/geometry`. Everything ran in the cloud (CPU, lavapipe); the laptop's GPU
stays paused at the owner's request.

## 0. Result

The owner: "why does our lighting look so nasty, instead of soft and gentle as in the reference?" Measured on the
video's frames against FellowEngine's render of the same moments, ours was:

* 2-3 times darker in the shade, the median half as bright;
* its fine detail half as contrasty again;
* its colour a quarter more saturated;
* its shadows knife-edged.

Decision 0300:

* **The light**: a soft sun (2.5° across); the shade's sky and bounce light doubled; exposure 1.3; less SSAO, contrast
  and saturation.
* **The stone**: dressed and warm — only its joints sunk, its photograph's baked shading blurred.
* **The coppi**: real tiles — tapered, each with a lip stepping over the course below, pan tiles in the channels; the
  clay smooth terracotta.
* **The glass**: dark — no sun mirrored in it, the sky's reflection mostly hidden, no lamps by day.

Ours against the video's, on frames 25, 57 and 89 (the mean of the ratios):

| | before | after |
|---|---|---|
| luminance percentiles (5th-95th) | 0.37-0.76 | 0.91-1.05 |
| fine detail | 1.59 | 1.31 |
| saturation | 1.27 | 0.97 |

## 1. Checks (cloud)

* The reference shot at the video's frames 25, 57, 89 (and 10, 45, 70, 84, 93) beside them, after each change, with
  the statistics.
* The square's opening shot: soft and bright, nothing burnt out.
* No motion, collision or engine change: the animation gates stand as at milestone 158 (38 of 38).

## 2. Next

* **The camera and the house measured through it.** A camera solved from the video's background (structure from
  motion, 0.5 px) cranes up the house. The house is taller than the replica, and its front is laid out otherwise:
  * the arched door near the left corner, a tall shuttered window over it;
  * the climbed window's sill about 2.4 m up;
  * the eave about 5 m up.

  The replica, its street (flagstones, where ours is cobbled) and the performance are to be measured again through it,
  frame by frame.
* The GPU, when the owner allows it.
