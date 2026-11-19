# The showcase's animation: a quality loop

Status: **IN USE** from milestone 155 (milestone 156: 38 of 38 gates pass; milestone 155 ended at 29 of 32, milestone
154's build at 14 — see the log below). Owner: the showcase (`showcase/`); the engine's part is the climbing's pose
(`FellowTraversalPose3D`). Every change to the mannequin's clips, its animation tree or its pose goes through this
loop; nothing is shown to the owner until its gates pass.

## Why

Milestones 150-154 changed the character's motion by eye: a change, a few frames looked at, a video. The owner, of
milestone 154's video: "these over-straightened legs are supposed to be realistic?" They were not, and nothing had
measured it. Read off the bones after the modifiers (milestone 154's build):

| | knee within 5° of straight | knee median | knee at its most bent |
|---|---|---|---|
| walk (1.6 m/s) | **58 % of frames** | 3.6° | 59° |
| run (4.4 m/s) | **32 %** | 36° | 110° |
| sprint (7 m/s) | **31 %** | 40° | 124° |
| mantle | 23 % | 63° | 149° |
| hanging, moving hold to hold | 0 % | 51-62° | 86-95° |

A walking knee is straight only at the heel's strike and bent 35-40° as the toe leaves the ground; a running knee is
never straight. The cause is in the clips' generator (`tools/blender/mannequin_anims.py`, `gait_pose()`): the stance
ankle is moved over the whole ground the body covers in a stance (1.04 m walking, 1.11 m running) — at the heel's
strike 0.57 m ahead of the hips and 0.83 m below them, 1.0 m away for a leg of 0.86 m. The leg's IK cannot reach: it
locks the knee straight and leaves the foot short of where it should be (which is also part of the foot's slide).

## The loop

```
  reference bands ──┐
                    ▼
  change ──► rebuild clips and scenes (3 min) ──► measure (anim_quality.gd, 1-2 min) ──► gates?
    ▲                                                                                     │ no: the worst metric,
    │                                                                                     │ its cause, the next change
    └─────────────────────────────────────────────────────────────────────────────────────┘
                                                                                          │ yes
                                                                                          ▼
                          contact sheets + curves against the bands (10 min) ──► rubric review ──► video ──► owner
```

1. **Reference** (fixed before the first change): joint angles over the cycle from gait analysis (below), and for
   what gait analysis does not cover (climbing, mantling, landing) stills the owner chooses from Assassin's Creed
   footage, kept out of the repository and used only to compare against.
2. **Measure** (`showcase/tools/anim_quality.gd`, headless, deterministic, in the repository): runs fixed programs
   — walk, run, sprint, speeding up and slowing down between them, a stop, a 90° turn at a run, a roof's slope, the
   climb up CasaCrema, a mantle, a leap — and reads the bones after the modifiers every frame. It writes a JSON and a
   text report:
   * the knee's, hip's and ankle's flexion over the stride (in 2 % bins from the foot's contact) against the
     reference band: the share of the stride inside the band, the largest miss;
   * the knee within 5° of straight (share of frames) and any hyperextension;
   * the skate: how far a planted foot's contact (the sole under the heel or the ball, whichever bears) moves in a
     stance; the feet through the ground;
   * the joints' angular speed through each change of gait or state (a spike is a pop); milestone 156: every frame, the
     fastest joint of the body (each bone's turn against its parent: the spine, neck, head, arms, hands, legs) and the
     hips' acceleration, per segment (p95, p99, the most), with the worst moments' bone, frame and state;
   * the left and right strides half a cycle apart (± 3 %);
   * climbing: the gripping hands to their holds, the braced feet to the wall, the knees braced, the body's distance
     from the wall, the look (decision 0293);
   * plots: each joint's curve over its band, one PNG per gait.
3. **Gates** (all must hold; a gate is changed only with the reason written in the decision record):

   | | gate | milestone 154 |
   |---|---|---|
   | walk knee | straight (< 5°) ≤ 12 % of the stride, only around the heel's strike; 5-20° after it (loading); 30-45° as the toe leaves; 55-70° at its most bent in the swing | 58 % straight; ~4° at toe-off |
   | run knee | never < 15°; 15-45° at contact; 40-65° at its most bent in stance; 90-125° in the swing | 32 % straight |
   | sprint knee | never < 15°; 110-130° in the swing | 31 % straight |
   | hip (walk) | 25-35° flexed at the heel's strike, 10-30° extended at the toe's push | to measure |
   | ankle (walk) | 10-25° plantarflexed after the strike; 15-30° as the toe leaves | to measure |
   | feet | skate ≤ 2 cm a stance in a steady gait (median), ≤ 5 cm through a change (the worst stance); never > 2 cm into the ground | 1.7-4.2 cm; 7.7-20.6 cm; -2.2 cm |
   | climbing | no knee within 5° of straight while braced or mantling; gripping hands ≤ 2 cm (decision 0291) | mantle 23 % |
   | transitions | no joint faster than 1.5× its fastest in either gait during a blend | to measure |
   | pops (milestone 156) | climbing (the run and jump at the wall, hanging, hold to hold, the mantle, the roof): no joint faster than 2200°/s against its parent; the mantle's hips' acceleration ≤ 400 m/s² | 3000-8900°/s; 4650 m/s² |
   | no regression | every other metric no worse than the last accepted build; `climb_check.gd` value for value | — |

4. **Change** — one cause at a time, the worst failing gate first; each change rebuilt and measured before the next.
   An iteration that does not move its gate, or breaks another, is undone. Three iterations without progress on a
   gate: the approach is wrong — stop and write down why before trying another (e.g. procedural → a different
   generator).
5. **Look** (only once the gates pass): contact sheets rendered on lavapipe from the side, the front and three
   quarters at ten moments of each stride, and each climbing move's key moments, next to the joint curves and the
   reference stills; judged against a written rubric (the knees, the hips' height and swing, the feet's roll, the arms'
   counter-swing, the head; for climbing the hands on the stone, the feet on the wall, the body's weight). Anything the
   rubric fails goes back to step 4 with a metric added for it, so that it cannot come back unseen.
6. **Owner**: the video at the real speed with the numbers that changed; the owner's remarks become gates.
7. **Regression**: `anim_quality.gd` runs before every commit that touches the clips, the tree, the player or the
   pose; its report's numbers go into the commit and the decision record.

## Running it

```
python3 showcase/tools/anim_quality.py run --godot bin/godot.linuxbsd.editor.x86_64 --out <dir> [--baseline <dir>]
```

`run` measures (two headless programs, about a minute) and reports; `report --out <dir>` reports again. A gate is
PASS, FAIL, or REGRESSED (further from its band than the baseline's value by more than its tolerance). The clips are
rebuilt with `tools/blender/build_mannequin.py` and the scenes with `tools/build_rooftops.gd` (README).

## The log

Milestone 155 (decision 0295): the gaits rebuilt from the supporting leg, soft IK, `FellowFootLock3D`.

| iteration | change | gates | what it showed |
|---|---|---|---|
| 0 | milestone 154 | 14/32 | the knees locked straight; the feet 2.2 cm in the ground; 7.7-20.6 cm dragged through changes |
| 1 | the hips from the supporting legs at a knee curve; soft IK; the swing from the toe-off | 18 | knees bent; the slide measured as speed misread a planted foot (reads at the physics ticks: read where drawn) |
| 2-3 | the hips capped by every planted foot's reach; the blend eased; the swing at the ground's speed | 18 | the pops gone; the slide still high — measured on the ball's joint, which rolls |
| 4-9 | the toe kept down while lifting; the clips at 60 Hz; the ball's pivot on the sole; the pivots turned with the foot; a jog | 24 | the skate measured on the sole: the steady gaits 1.9-3.7 cm a stance; the changes 12-20 cm (a walk's planted foot blended with a run's swinging one) |
| 10-12 | `FellowFootLock3D` (the ankle held, then the sole; grabbed within 1.2 cm) | 25 | the changes 2.2-2.4 cm; holding the ankle had bent the toe-off knee |
| 13-16 | soft IK in the climbing's pose; the lock reaching what the clips reach; a reach margin in the push | 25 | the mantle's knees no longer straight |
| 17 | the walk's stride as in iteration 12 | 25 | the walk's toe-off knee in its band |
| 18 | running, the foot reaching the ground at 70 % of its speed (was 45 %); the sprint's swing without its heel kick | 29 | the running skate and knee, the sprint's skate and the changes' pops in their bands |

Milestone 156 (decision 0296): the owner's reference video; the whole body's smoothness measured.

| iteration | change | gates | what it showed |
|---|---|---|---|
| 1 | the new mannequin; the run's lean and arms; the crawl; the foot flat earlier after the strike | 30/32 | the smoothness read at last: pops of 3000-9400°/s (the jump at the wall, the mantle's start and end, a leap's forearm), the mantle's body 0.65 m in two frames |
| 2 | the clips' rig blending FK to IK; `clip_blend`; the mantle let go from the wall over 18 % of it | 30/32 | the jump at the wall 5001 → 1854°/s, the mantle's knee 8863 → 3534, its body 4653 → 197 m/s² |
| 3 | `pole_follow`; the crawl's arms within reach | 30/32 | the crawl 2098 → 1366; a leap's forearm turned about its own axis (6405) |
| 4-5 | `max_joint_speed` (1500, then 2000: 5 % of the climbing's frames had reached 1500) | 30/32 + 6 new | every climbing segment ≤ 2087°/s |
| 6-8 | the foot lock's way back within the leg's reach (not the hold: the walk's planted foot let go) | 36/38 | the run from standing 2804 → 1343°/s; the sprint's knee at the toe-off 12.7 → 14.9° |
| 9 | the walk's hips 8 mm higher; the sprint's knee 34° at the toe-off | 38/38 | the walk's knee at the strike 20 → 15° |

Next: the feet on slopes (the roofs' tiles walked, not only crawled), starts and stops, landings — each with its gates
first; the run's lean and arm swing against the reference's frames as gates (read off a pose estimate, if the owner
allows one).

## Reference

* Walking (normal gait): the knee 0° at the heel's strike, 5-10° in loading and mid-stance, 30-40° at pre-swing,
  65° at its most bent in the swing; the hip 30° flexed at the strike, 15-30° extended at terminal stance; the ankle
  20° plantarflexed in loading, 15° dorsiflexed in terminal stance, 20-30° plantarflexed at the toe-off — University
  of Missouri, PM&R, *Normal Gait*.
* Running: the knee about 40° at contact, 60° in loading, about 40° before the toe-off, 125° at its most bent in the
  swing; the hip about 50° flexed at contact, 10° extended after the toe-off — Physiopedia, *Running Biomechanics*.

Sources: [Normal Gait (University of Missouri)](https://medicine.missouri.edu/sites/default/files/pm-r/Normal_Gait.pdf),
[Running Biomechanics (Physiopedia)](https://www.physio-pedia.com/Running_Biomechanics).
