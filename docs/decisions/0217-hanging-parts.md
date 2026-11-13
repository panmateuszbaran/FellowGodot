# 0217 — Parts that hang before they come off

Status: **KEEP** (`FellowVehicleDamage3D.hang_depth`, 0 by default: parts come off at once, decision 0216's) · Branch
`fellow/geometry` · Code: `modules/fellow_world/fellow_vehicle_damage_3d.{h,cpp}` · Checks:
`benchmarks/project/bench/vehicle_check.gd` (the lane `hang`).

## Problem

Decision 0216's parts are on or off. In the genre's games a part is often half off first — a bumper hanging from one
end and scraping along the road, a door swinging — and it comes off at the next hit; that stage is what makes a damaged
car read as damaged while it still drives.

## Change

* **`hang_depth`** (m, 0: off): at it a part comes loose — its body is made as decision 0216's — and is pinned to the
  vehicle by a `PinJoint3D` at the end of its longest side farthest from its last hit (a bumper hit at its right end
  hangs from its left end and swings down). Its dents keep adding up; at `detach_depth` the pin lets go and it comes
  off. `part_hanging(part, body)`, `is_part_hanging()`; `part_detached` follows with the same body; `repair()` frees
  the pin too.

## Checks (cloud: the CPU; the editor build and the release template the same)

The lane `hang`: decision 0216's bumper on a car held at rest, `hang_depth` 0.05 m and `detach_depth` 0.15 m, hit at
its right end 8 cm deep at 0.5 s and again at 3 s (where that end hangs then), repaired at 6 s:

| expected | result |
|---|---|
| at 2.5 s hanging (not off), its left end where it was (within 5 cm), its right end down by more than 10 cm, the pin there | hanging; the left end 2.5 cm from where it was, the right end 27.3 cm down (on the ground) |
| at 5.5 s off (the same body), on the ground, the pin gone | off; y 0.10 m; no pin |
| after `repair()` on the car where it was, no body or pin | so |

The other 27 lanes the same values as before (decision 0216's lane: the part's body's name no longer compared — two
lanes' bodies share one parent).

The laptop's first run (job f414) hit the second time where the bumper had been before it hung: with GodotPhysics3D
the hanging bumper was still close enough to reach 0.15 m, with Jolt (it hung 9 mm lower) not — a hanging part is
dented only where it is. The lane now hits it where it hangs: job f415 — every lane passes with GodotPhysics3D (the left end 2.5 cm from where it
was, the right end 27.3 cm down, off at the second hit, y 0.10 m) and with Jolt (2.6 cm, 28.2 cm, y 0.09 m).

## Decision

Keep: a part hit once hangs; hit again, it falls.

## Rejected / not done

* **Hinges along a side** (a door on its hinges, a bonnet): a pin at one end — the part swings every way about it.
* **A part torn off by its own swinging** (the joint's force): only the dents decide.
