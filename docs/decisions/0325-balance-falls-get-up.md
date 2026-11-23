# 0325 — Balance: a hit's capture point decides — taken back, a stagger, a fall; and getting up

Status: **KEEP** (opt-in: `FellowPhysicalAnimation3D.balance`, off by default; off, the node is milestone 166's) ·
Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_physical_animation_3d.{h,cpp}`: `balance`, `motion_matcher_path`,
  `step_threshold`, `fall_threshold`, `topple_angle`, `get_up_tag`, `auto_get_up`, `get_up_delay`, `get_up_blend`;
  `get_state()`, `get_landed_face()`, `knock_down()`, `get_up()`, `get_balance_stats()`; the signals `staggered`,
  `fell`, `landed`, `got_up`;
* `modules/fellow_animation/fellow_motion_matcher_3d.{h,cpp}`: `push(velocity, duration)`, `is_pushed()`;
* `benchmarks/project/bench/balance_check.gd`, `bench/balance_video.gd` (new); `scenes/matching_lab.gd` (subjects 139
  and 140 tagged `get_up`); `benchmarks/scripts/get_cmu_locomotion.sh` (their trials).

Checks: `balance_check.gd` on Jolt Physics (headless), `physical_check.gd` (milestone 166's, balance off), a character
filmed on lavapipe. No GPU.

## Problem

Decision 0307's physical animation lets a hit knock a part and pulls it back; at strength 0 the body is a ragdoll. But
a character pushed hard enough to be thrown off balance stayed where it stood, its bodies pulled back to an animation
that did not know it had been pushed; and once a ragdoll it lay there for good. AAA characters stagger when shoved,
fall when they cannot catch themselves, and get up again (Euphoria in GTA and Red Dead Redemption, Unreal's physical
animation with get-up montages).

## Change

With `balance` on and `motion_matcher_path` set:

* **The decision: the capture point** (Pratt et al. 2006): the point the centre of mass must be stood over to stop —
  its offset from the animation's plus its velocity over √(g / h). At a hit the node takes the bodies' centre of mass,
  the animation's, and the hit's velocity for the whole body (impulse / mass), and finds how far the capture point
  goes:
  * under `step_threshold` (0.12 m, about a foot's support): the drive takes the hit back where it stands
    (decision 0307's);
  * further: **a stagger** — the motion matcher is pushed that way (its new `push()`: for 2.6 / ω s, about 0.8 s, it
    asks for a velocity decaying to nothing, its facing held: CMU's back and side steps), as far as the capture point;
  * beyond `fall_threshold` (0.7 m, a step's reach): **a fall**.
* **The push spread over the body**: with balance a hit's impulse goes to every body by its mass (the joints carry a
  held body), the struck one as much more as kicks it 1.5 m/s faster (at most 30 %); for a fall, all spread.
* **The fall**: stiff at first — the pose it stood in turned about its feet (their edge on the side it falls to) as an
  inverted pendulum falls (θ'' = g / h · sin θ, from the push's speed), the drive holding the bodies to it — then
  limp past `topple_angle` (40°). The motion matcher stops.
* **Down**: lying still (the bodies slower than 0.25 m/s on average for 0.3 s), the face it lies on from its trunk
  (up the spine crossed with the hips' left to right): on its back, its face or a side; `landed(face)`.
* **Getting up**: after `get_up_delay` (1 s) the motion matcher's database's clips tagged `get_up_tag` (left out of its
  search: lying still matches standing still better than any frame) are tried: each from a tenth of a second before
  its first bone moves 5 cm, its bones turned about the vertical and shifted over the ground to fit the bodies' (a
  least-squares fit seen from above, by mass), on the face the body lies on; the one with the least mean distance left
  is played, the bodies' targets blended from where they lie into it over `get_up_blend` (0.5 s), the ground under the
  hips found by a ray. When it stands (0.6 s after its hips reach standing height) the motion matcher is put where the
  clip ends, facing its way, and its pose blended in over 0.4 s: it walks on. `got_up`.
* The joints made anew from the bodies' places (going limp, getting up) first put each body back on its parent's joint
  (a joint pulled open by a hit or an impact had been made anew that far open).

The clips: CMU's subject 140 (getting up face down, on a side, on the back: trials 1-4, 8, 9) and subject 139's trials
16-18, nine clips.

## Measurements

`balance_check.gd` (Jolt, 60 Hz, the CMU locomotion database with the nine clips, a character standing, a push at the
chest from the front):

| push | capture point | what follows | went | the joints' largest gap |
|---:|---:|---|---:|---:|
| 20 N·s | 0.08 m | taken back | 0.07 m | 16 mm |
| 40 | 0.17 | a stagger (0.77 s) | 0.16 | 43 |
| 60 | 0.25 | a stagger | 0.10 | 40 |
| 80 | 0.34 | a stagger | 0.28 | 50 |
| 100 | 0.42 | a stagger | 0.37 | 54 |
| 130 | 0.55 | a stagger | 0.59 | 61 |
| 160 | 0.67 | a stagger | 0.64 | 58 |
| 200 | 0.84 | a fall, on its back; down 1.05 s, getting up 2.07 s, standing 7.42 s | 1.72 | 87 |

Falls (200 N·s) from every side:

| pushed from | lands | down after | the clip | its start fits within | blending, fastest body | getting up | hips standing | after, the bodies from the animation |
|---|---|---:|---|---:|---:|---:|---:|---:|
| the front | on its back | 0.90 s | 140_08 (on the back) | 0.123 m | 1.74 m/s | 5.35 s | 0.997 m | 1.7 mm |
| behind | on its face | 0.98 | 139_16 | 0.070 | 1.61 | 2.98 | 0.995 | 2.0 |
| the left | on its face | 0.95 | 139_16 | 0.090 | 2.25 | 2.98 | 1.000 | 2.1 |
| the right | on its face | 1.32 | 139_17 | 0.087 | 2.31 | 4.63 | 1.014 | 2.0 |

* Walking at 1.3 m/s, 70 N·s from the side: a stagger (capture point 0.29 m), walking on after 0.75 s.
* The drive 12.5 µs a step (decision 0307's 11.5-16).
* Off (`physical_check.gd`, Jolt): decision 0307's numbers — the bodies 3.7 mm from the walk on average (3.6), the
  chest knocked 104 mm (102) and the head 226 (212), the ragdoll at 4.3 m/s (4.6) and 46 mm into the floor (46); the
  differences are the animation's changes since milestone 166 (the code paths with balance off are the same).

## How it got there

* The get-up clips' still frames lying down matched the motion matcher's standing still better than any: the
  character lay down at the start. They are left out of its search as soon as its database is there.
* The first get-ups shot the bodies at 100 m/s: the drive had still targeted the standing pose for a step (and put
  the bodies back there), with feed-forward from it. The targets now start at the bodies, without feed-forward.
* Pushed from behind, the ragdoll came apart on landing (a forearm and the head 30 cm away): all of the impulse had gone
  to the chest (230 N·s on 10 kg, 23 m/s) and the struck body, weakened, was not driven with its neighbours. The push is
  spread over the body, all of it for a fall, and a fall weakens nothing.
* On its back the character first got up with 139_18 (a clip starting on a side): from above, lying on the back and on
  a side fit alike, and blending through the turn crossed its legs. The clips must start on the face the body lies on.
* Lying still was found after 1.5-3.4 s (every body under 0.3 m/s: one limb was always settling): by their mean now,
  after 0.9-1.3 s.
* The joints still open 4-11 cm as a body falls and lands (Jolt's two position steps against a chain of 21 bodies); six
  position steps closed the lying body's to 1.4 cm, a project's choice. An extra joint pass by velocity and collision
  exceptions between joined bodies made no difference and were taken out.

## Decision

KEEP, opt-in: a character pushed staggers as far as its capture point, falls when it cannot catch itself, and gets up
with the clip that fits how it lies, then walks on.

## Rejected / not done

* Bracing: the arms reaching out towards the fall (the stiff fall keeps the pose it stood in).
* A stagger's steps placed by the capture point (the motion matcher chooses them from its back and side steps).
* Getting up from a slope, against a wall, under something; a clip for every face (subject 140 has two each; 139's
  three are taken as face down and on a side).
