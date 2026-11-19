# 0294 — The stride and the edge: gaits blended in step, the body leaning into turns, no step off a roof

Status: **KEEP** (the showcase) · Branch `fellow/geometry` · Code: `showcase/tools/blender/mannequin_anims.py`
(`gait_walk`, `gait_run`, `gait_sprint`), `showcase/tools/build_rooftops.gd` (`_anim_tree()`'s `loco`, `LOOPS`),
`showcase/scripts/rooftops_player.gd` (`_loco()`, `_gait_cycle()`, `_lean_model()`, `_edge_guard()`) · Checks: the planted
foot's slide through a program of gaits (the bones after the modifiers), the edge at a beam, an eave and a gable, the
demo's shots timed.

## Problem

The owner, of milestone 153's showcase: "the movement's animations are still not very realistic" and "it is easy to
fall off when on an edge and pressing a move to the side — that should be guarded against".

* **The gaits.** Idle, walk, run and sprint were points of one blend space, each clip its own length (1.08, 0.7 and
  0.62 s) played at its own rate. Blended (accelerating, slowing, any speed between the points) the two clips were at
  different moments of their strides — one foot planted in one, swinging in the other — and played at a rate that was
  neither's: the feet slid and the legs blurred. Measured (the planted foot's ground speed, accelerating from a walk to
  a run, a run to a sprint and stopping): 0.95, 1.29 and 1.44 m/s on average.
* **The body** stayed upright through turns and changes of speed, its head looking straight ahead.
* **The edge.** Walking or running, nothing stopped the body at a roof's edge: a press to the side at the eave walked
  it off, 11 m into the street.

## Change

* **Gaits in step**: each gait's cycle normalized to 1 s (`gait_walk`, `gait_run`, `gait_sprint`; the same motion)
  and blended at the same moment of the stride (the blend space's sync), the blend played at the cycle's rate for the
  body's speed (`1 / cycle(v)`, the cycle between the gaits' own); idle blended in below 0.6 m/s.
* **The lean**: the model (not the capsule) tipped about its feet into a turn (the turn's rate times the speed, up to
  14°) and forwards as the speed grows, back as it falls (up to 8°), eased (0.12 s).
* **The head** turned to where the controller steers (decision 0293).
* **The edge** (`_edge_guard()`): walking or running (not sprinting — free-running is the way off a roof), where the
  ground ends within 0.42 m the way the body goes (a drop of more than 0.7 m), the edge's way out is found (the ways
  round that half of the body with no ground) and the speed loses its part along it: the body stops at the edge, or
  goes along it if it was slanting. Only the way the body goes is looked at: a beam or a ridge is walked along whatever
  lies either side. Hanging, a shimmy already stopped at a ledge's end (checked: 6 s pressing towards each end of
  CasaCrema's impost band, still hanging).

## Checks

* **The planted foot's slide** (the toe or the heel on the ground, the slowest; walk 3 s, run 2.7 s, sprint 2 s and
  the changes between; m/s, mean / 90th percentile):

  | | before | after |
  |---|---|---|
  | walk | 0.08 / 0.16 | 0.04 / 0.11 |
  | walk to run | 0.95 / 3.70 | 0.50 / 1.18 |
  | run | 0.18 / 0.44 | 0.22 / 0.62 |
  | run to sprint | 1.29 / 3.30 | 0.35 / 1.20 |
  | sprint | 0.42 / 1.27 | 0.44 / 1.82 |
  | sprint to stop | 1.44 / 5.28 | 0.73 / 5.00 |

  The changes between gaits slide half as much or less; the steady gaits are their clips (the same motion, the same
  rate; their slide is the heel's and toe's roll). The feet never crossed.
* **The edge**: CasaCrema's eave walked or run at straight on — stopped 0.45 m from it; at a slant — along it to the
  gable's end and stopped there; sprinted at — over it (into the street, as before); the belfry's beam — along it to its
  end, a press to its side or at a slant stops the body on it.
* **The demo**: every shot in its time (the walk out along the beam reaches its end; with the first try below, the
  drops either side of the beam stopped it at the tower).
* **Found on the way**: the 1-s gait clips had not been listed as looping — the run's pose froze after one cycle.

## Decision

KEEP.

## Rejected / not done

* **The edge guard looking all round the body** (the first try): on a beam the drops either side add up to a way out
  ahead and the body stopped where the beam begins.
* **Feet on slopes** (the roofs' tiles: the feet placed by IK on the ground under them): next.
* **A start and a stop of their own** (AC III's sprint propulsion: the first steps a push, the last a plant): next.
