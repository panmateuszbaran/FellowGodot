# 0196 — Vehicles on loose ground and marks that fade

Status: **KEEP** (on `FellowVehicleEffects3D`: dust and ruts only on colliders in `loose_ground_group`;
`mark_lifetime` 0 by default — nothing changes on other ground or with the defaults) · Branch `fellow/geometry` ·
Code: `modules/fellow_world/fellow_vehicle_effects_3d.{h,cpp}`, `fellow_vehicle_3d.h` (the wheel's ground) · Checks:
`benchmarks/project/bench/vehicle_fx_check.gd` (seven lanes: decision 0187's five unchanged, two new), the regression
scene `vehicle`.

## Problem

Decision 0187's "Next": dust and gravel on loose ground, marks that fade with time. A rally car on a dirt road raises
a plume and leaves ruts while it rolls, not only when it slides; marks that never fade fill a race track's corners and
show a ring buffer's reuse as marks vanishing abruptly.

## Change

* **Loose ground**: a collider in the group `loose_ground_group` ("fellow_loose_ground") under a wheel (the wheel's
  ground, `FellowWheel3D::get_ground_id()`). There:
  * **dust** (`dust`, on): a GPU particle system at each wheel (made with the node: no pipeline compiled mid-game),
    `dust_amount` (48) over `dust_lifetime` (1.6 s), thrown up and drifting, growing; its intensity the vehicle's speed
    over the ground from `dust_min_speed` (2 m/s) to `dust_full_speed` (15 m/s), or the tyre's sliding if more; its
    colour `dust_color`, or the ground collider's metadata `fellow_dust_color`. The tyre's smoke is not made there.
  * **ruts**: a rolling tyre lays marks at `rut_opacity` (0.3), a sliding one at the skid marks' opacity if more.
* **Fading** (`mark_lifetime`, 0: as before): a segment's birth and opacity kept; four times a second the segments past
  half their lifetime are faded (linearly to none at its end), their colours written in one update when one changed.
* `get_stats()`: `dusting`, `on_loose_ground`, `visible_segments`.

## Checks (cloud: the CPU, lavapipe)

`bench/vehicle_fx_check.gd`, the editor build and the release template, GodotPhysics3D and Jolt: the same results:

| lane | expected | result |
|---|---|---|
| cruising at 50 km/h on a strip of loose ground | dust and ruts, no smoke | the four wheels on it; dust in 795 frames, 1 520 rut segments (the ring's 512 shown), no smoke |
| a handbrake turn with `mark_lifetime` 3 s | its marks, then none | 139 segments laid (as decision 0187's turn), all faded by the end |

Decision 0187's five lanes: the same numbers (139, 138 and 360 segments; none cruising or with the anti-lock brakes).
Lavapipe: filmed (the plume and ruts on the strip; the turn's marks gone at 13 s while the others' stay). The
regression scene `vehicle`: exact, the release template the same.

## Decision

Keep: cars on dirt raise dust and leave ruts with a group on the ground; marks can fade.

## Rejected / not done

* **Gravel thrown (bodies or particles colliding)**, surfaces by physics material, per-wheel dust colours: one colour
  a vehicle at a time.
* **Deformed ground** (ruts as geometry): marks on the surface only.
