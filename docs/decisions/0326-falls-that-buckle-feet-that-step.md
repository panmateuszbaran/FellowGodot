# 0326 — Falls that buckle, feet that step: the legs give way over the feet, a foot that would slide steps, the get-up handed back in place

Status: **KEEP** — the balance's fall (decision 0325, opt-in) buckles wherever the motion matcher's database knows the
legs; `FellowMotionMatcher3D.foot_step_lift` on (0.08 m; 0: milestone 183's feet) · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_physical_animation_3d.{h,cpp}`: `_fall()` (the speed it begins with),
  `_fall_parts()`, `_fall_pose()`, `_fall_leg()`, `_fall_hips_at()` (new), `_update_balance()` (the falling state),
  the hand-over after getting up; `topple_angle` 40 → 60°; `get_balance_stats()` adds `fall_buckles`,
  `fall_limp_seconds`, `fall_limp_hips_m`, `fall_limp_lean_deg`, `fall_kneeling`;
* `modules/fellow_animation/fellow_motion_matcher_3d.{h,cpp}`: `foot_step_lift` (new), `play_from()` (new),
  `get_stats()`'s `foot_slide_raised`;
* `benchmarks/project/bench/balance_check.gd` (the feet's slip, the fall's timing, `--trace`),
  `scenes/matching_lab.gd` (`--matching_step_lift`).

Checks: `balance_check.gd` on Jolt Physics (headless), Matching Lab's crowd (`matching_eval.gd`), `physical_check.gd`
(balance off), the character filmed on lavapipe. No GPU (the owner's wish).

## Problem

The owner, of milestone 183's film: "it sometimes slides unnaturally, and the falls are very sudden". Measured
(`balance_check.gd`, new metrics: a foot's slip is how far it moves over the ground while within 3 cm of its standing
height, each stance's farthest from where it began, summed):

* **The falls**: the hips reached the ground (0.3 m) 0.37 s after the push, at 3.6-4.3 m/s — as fast as the hips
  dropped from where they stood (0.38 s to 0.3 m). The whole push's speed (2.9 m/s for 200 N·s) started the stiff topple; at
  40° it went limp as a plank, and as it landed the feet were dragged 46-95 cm. People fall from standing in about
  0.7 s (the pre-impact phase: Hsiao and Robinovitch 1998, J. Biomech. 31(1); Le Goic et al. 2018, Front. Neurol.
  9:986), the hips landing at 2.75 ± 0.42 m/s (van den Kroonenberg, Hayes and McMahon 1996, J. Biomech. 29(6)): they
  give at the knees, sit or kneel into the fall and reach out with their arms.
* **Getting up**: once the clip ended the whole body slid 37-65 cm over 0.4 s (the feet 74-101 cm). The motion matcher
  was put where the clip's *root* ends, with a pose of its own; but the database keeps the hips relative to the root,
  and the get-up is played with its root held at the anchor — the root's path was added a second time.
* **Standing and staggering**: a foot crept 14-21 cm over the ground. Its lock let go when the animation (a transition
  between two stances, a stagger's last steps) was `unlock_radius` away while the foot was still on the ground, and
  took it again the next update a little nearer — a ratchet; the database's own drags (a foot shuffled a few
  centimetres up) slid too.

## Change

* **The fall buckles** (with the legs' roles: the motion matcher's database's thigh, shin and foot): the targets are the
  pose the body stood in, its parts moved three ways —
  * the hips on the line from the feet: after a reaction's time (0.12 s) they sink along it, the speed rising to
    2 m/s (time constant 0.22 s), and the line leans as an inverted pendulum about the feet (θ'' = g / l · sin θ, l the
    line's length as it shortens; it starts as fast as the centre of mass would still move after the farthest step:
    ω (c − `fall_threshold`), not the push's whole speed); the upper body leans with it;
  * each leg bent by two-bone IK between the hip and its foot, which stays where it stands, the knee forward;
  * the arms (the trunk's branches holding no leg, from the upper arm) turned about their shoulders towards the ground
    ahead of the fall and to their side, from 0.1 s over 0.35 s (a protective response's latency).

  It goes limp when the hips are 0.38 m over the ground, when the knees would reach the ground (falling forward: the
  ragdoll lands on them and falls on over them) or past `topple_angle` (now 60°). Without the legs' roles, decision
  0325's stiff fall.
* **The get-up handed back in place**: `FellowMotionMatcher3D.play_from(frame, position, facing)` (new) puts the
  motion matcher at the clip's anchor showing the clip's last frame as it is; its next search, by the trajectory alone
  (by the pose it found a crouch: the clip ends in a wide stance, the hips low), finds what follows, blended in as any
  transition, the feet held by foot locking.
* **Feet that would slide step** (`FellowMotionMatcher3D.foot_step_lift`, 0.08 m, with `foot_ik`): a foot let go of
  on the ground because its animation is `unlock_radius` away is not held again until it has stepped there, lifted on an
  arc (a parabola over the way, 40 % of it high, at most `foot_step_lift`); a foot let go of more than 4 cm from its
  animation as it lifts gets the arc on top of its own lift; a foot the animation drags along low while the character
  stands is lifted by 0.12 s of its speed (at most half of `foot_step_lift`, a 0.05 s spring). `get_stats()` counts
  the part of the slide with the foot raised 1 cm or more (`foot_slide_raised`: stepping).

## Measurements

`balance_check.gd` (Jolt, 60 Hz, the CMU database with subjects 139 and 140's get-up clips, pushes at the chest):

Falls (200 N·s), milestone 183 → 184 (the same metrics; milestone 183 measured at the start of this milestone):

| pushed from | the hips at 0.3 m | coming down at | went limp (hips, lean) | the feet's slip (all, one stance's most) | lands | lying still | the clip, its start's fit | standing after |
|---|---:|---:|---|---:|---|---:|---|---:|
| the front | 0.37 → **0.62 s** | 3.63 → **2.61 m/s** | 0.57 s (0.36 m, 34°) | 46.0 / 16.0 → **5.1 / 2.7 cm** | on its back | 1.28 s | 140_08, 0.105 m | 5.35 s |
| behind | 0.37 → **0.60** | 4.34 → **2.74** | 0.48 (0.55 m, 24°, the knees at the ground) | 93.7 / 45.2 → **9.0 / 5.7** | face down | 1.42 | 140_02, 0.270 | 5.17 |
| the left | 0.37 → **0.62** | 3.88 → **2.64** | 0.57 (0.36 m, 34°) | 70.8 / 44.7 → **10.2 / 7.0** | face down | 1.30 | 139_17, 0.149 | 4.63 |
| the right | 0.37 → **0.62** | 4.04 → **2.64** | 0.57 (0.36 m, 34°) | 95.3 / 50.7 → **5.4 / 3.5** | face down | 1.30 | 139_16, 0.155 | 2.98 |

(People: about 0.7 s and 2.75 ± 0.42 m/s. The fall forward goes limp at the knees and lands on them, its hips coming down
over them; the others sit first. The hips slide on 5-21 cm after they land, from 21-36.)

* Staggers, the feet's slip (all, one stance's most): 60 N·s 4.3 / 2.6 → 2.7 / 1.6 cm, 100 N·s 41.6-56.1 / 21.0-29.3 →
  9.1 / 4.6, 160 N·s 38.7 / 18.1 → 13.0 / 4.2 (40-160 N·s: 1.1-18.7 / 0.8-8.7); they go as far as before (0.02-0.64 m)
  and stand again after 0.77 s.
* Standing still, a weight shift: 17.6 / 16.2 → 15.8 / 12.7 cm — now a step, the foot 6-8 cm up in its middle; the
  metric counts its low ends.
* After getting up, the motion matcher taking over: the hips stay where the clip leaves them (they slid 37-65 cm); the
  feet's slip 74-101 → 4-33 cm (they step into its stance; the same low ends counted).
* The joints' largest gap: staggers 43-61 mm (as before), falls 40-86 mm (50-111).
* Getting up: lying still 1.3-1.4 s after the push (0.9-1.3: the fall takes longer); the clips' starts fit within
  9-27 cm (7-12: a sitting or kneeling landing fits the clips' first frames less well); standing after 3.0-5.4 s, the hips
  0.93-1.00 m up, the bodies 1.3-3.7 mm from the animation after. The fastest body while blending in 2.0-7.8 m/s (a
  0.23 kg foot pressed into the ground for a quarter of a second).
* The drive 13.1 µs a step (12.5).

Matching Lab, 16 characters walking, jogging and running for 60 s (`matching_eval.gd`), `foot_step_lift` 0 → 0.08:

| | foot slide in contact (m/s) | of it raised (stepping) | on the ground | locked | pop p99 (mm) | transitions /s |
|---|---:|---:|---:|---:|---:|---:|
| 0 (milestone 183) | 0.0625 | 0 | 0.0625 | 0.0351 | 37.3 | 2.39 |
| **0.08** | 0.0834 | 0.0468 | **0.0366** | 0.0264 | 39.3 | 2.39 |

The feet move over the ground 41 % less; the rest of their motion in contact is now steps (the database's contacts
still on as a foot lifts a few centimetres to go where its animation is).

* With balance off, `physical_check.gd` (Jolt): the bodies 3.7 mm from the walk on average (3.7), the chest knocked
  104 mm (104) and the head 226 (226), the ragdoll at 4.35 m/s (4.3) and 52 mm into the floor (46: the walk's pose
  where it goes limp differs with the feet's steps).

## How it got there

* The first buckling model prescribed the hips' height and swung them out by the angle at that height: at 50° they were
  further from the feet than the legs reach, and the feet were dragged. The hips now stay on the line from the feet.
* Falling forward the knees, bent forward, reached the ground with the hips still 0.55 m up; turning them up instead
  bent them backwards and kicked a foot 60 cm. Holding the knee on the ground and drawing the foot to the shin dragged
  the feet 20 cm. Going limp there lets the ragdoll land on its knees.
* Put back by its hips (teleported again until the matcher's own pose's hips met the clip's), the get-up's end still
  glided its feet 30-46 cm between two stances. Played on from the clip's frame it matched a crouch by the pose and
  dipped its hips 20 cm; searching by the trajectory alone once found standing.
* The lift for drags first did nothing: the leg IK ran only for a foot locked or offset, not for one lifted.
* A debounce of the database's contacts (two updates) changed nothing: the creeping was the lock's ratchet, not
  flickering contacts. Holding off the lock after every let-go (also as a foot lifts) slowed a walking foot's landing:
  the crowd's slide rose; only a let-go on the ground holds it off now.

## Decision

KEEP. A pushed character that cannot catch itself now gives at the knees and sits, kneels or sprawls into the fall
with its arms out, landing about 0.6 s after the push at the speed people's hips land; its feet stay where they stand
until it is limp; getting up ends where the clip ends; and feet that would glide over the ground step.

## Rejected / not done

* A stumbling step before the fall (the motion matcher pushed into a step, then the buckle from the pose it reaches).
* Falling forward onto the hands with the knees on the ground driven (the shin pivoting on the toes): the ragdoll does
  it.
* Get-up clips from more subjects (the side landings choose among three).
