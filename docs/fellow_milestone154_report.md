# FellowEngine — milestone 154 report (a camera that does not shake; gaits in step; no step off a roof; the eyes on the next hold)

Status: **FINAL** (2026-11-19). Branch `fellow/geometry`. Everything here ran on the CPU and lavapipe in the cloud: the
laptop's GPU stays paused at the owner's request (`f518` held in `.tools/wip`).

## 0. Result

The owner, having run milestone 153's showcase on the laptop: "1. while moving the camera vibrates; 2. the movement's
animations are still not very realistic; 3. it is easy to fall off when on an edge and pressing a move to the side —
that should be guarded against; 4. the climbing's animation needs more work — maybe compare it with recordings of
Assassin's Creed?"

| the owner's words | what was wrong | what changed |
|---|---|---|
| "the camera vibrates" | The body moved at the physics ticks (60 Hz), drawn where the last tick left it; at 144 Hz it moved 0 to 7.3 cm a frame instead of 3.06, the camera following it | The player drawn between the ticks (physics interpolation), its animation advanced in the idle frames, the climbing's pose placed where the body is drawn (decision 0292): 3.06 cm every frame, the body and the camera |
| "the movement's animations are not realistic" | Walk, run and sprint blended at different moments of their strides and at a rate that was neither's: the feet slid, the legs blurred between gaits; the body upright through turns, the head fixed ahead | The gaits blended in step at the cycle's rate for the speed (the slide between gaits halved or less); the body leaning into turns and with its speed's changes; the head turning into a turn (decisions 0294, 0293) |
| "easy to fall off an edge pressing to the side" | Walking or running, nothing stopped the body at an edge | Walking or running it stops at an edge, or goes along it at a slant; a beam is walked along; sprinting (free-running) still goes over; hanging, a shimmy already stopped at a ledge's end (decision 0294) |
| "the climbing — compare with Assassin's Creed" | The climber looked straight into the wall whatever it did; the hands slid on their holds between the ticks | The climber looks at the next hold from a move's start (the wind-up included), along the ledge the way it shimmies, up the wall when it climbs, over the top (decision 0293); the hands on their holds at any display rate (0292) |

* **Decision 0292 (engine)**: `FellowTraversalPose3D` places its targets relative to the skeleton's interpolated
  transform (the same as before with physics interpolation off).
* **Decision 0293 (engine)**: `look` and `look_weight` in `FellowTraversal3D.get_pose_targets()`.
* **Decision 0294 (showcase)**: the gaits, the lean, the edge.
* **Found on the way**: the new 1-s gait clips were not listed as looping — the run's pose froze after one cycle (in
  this milestone's first build; found by the foot-slide probe, fixed before the measurements below).

## 1. Assassin's Creed, compared

Read: the summary of Assassin's Creed III's animation talk at GDC 2013 (Wolfire's notes) and a retrospective of its
animation team's work. What AC III does, against the showcase:

| AC III | the showcase |
|---|---|
| Climbing: the hands and feet placed by full-body IK on the holds | The hands on the holds by two-bone IK, the body moved until the shoulders reach them (0291); the feet braced on the wall under the ledge — not yet on the wall's own holds |
| The whole body reacts to each jump and landing (procedural) | A leap winds up and is caught with its speed, the hips swinging on a spring (0290); a landing has its clips (roll, hard landing), no procedural reaction yet |
| Sprint "propulsion": a start that pushes off | No start or stop of their own yet (the gait accelerates within 0.3-0.5 s) |
| Leaning into turns, the feet placed through them | The body leans into turns (0294); the feet are the clip's (no foot IK on the ground yet: on the roofs' slopes they are not placed) |
| The head and eyes where the character goes (seen in the footage) | The head turned to the next hold, the shimmy's way, the top, the steering's way (0293) |
| Equipment on springs; a driven ragdoll | Not in the mannequin |

What it leaves for next: the feet on the wall's actual holds (and a free hang where there are none: legs swinging
under an eave), the feet placed by IK on sloping roofs, starts and stops of their own, a procedural reaction to
landings.

## 2. Checks (cloud)

* **The drawn steps** (headless at `--fixed-fps 144` over 60 Hz physics, running at 4.4 m/s, 432 frames): before —
  the body 0-7.33 cm a frame (standard deviation 3.62 cm), the camera 1.18-4.55 cm (1.55); after — both 3.06 cm every
  frame (0.00).
* **The gripping hands at 144 Hz** (CasaCrema's climb): on their holds as at 60 Hz (median 0.9 cm); the skeleton's
  physics and drawn places differ by up to 7 cm (90th percentile 2.8 cm): what the hands would have been off with the
  old transform.
* **The planted foot's slide** (walk, run, sprint and the changes between; m/s, mean): walk 0.08 → 0.04, walk to run
  0.95 → 0.50, run 0.18 → 0.22, run to sprint 1.29 → 0.35, sprint 0.42 → 0.44, stopping 1.44 → 0.73. The feet never
  crossed. (Milestone 153 measured in a worktree of its commit with this build.)
* **The edge**: CasaCrema's eave — walked or run at straight on, stopped 0.45 m from it; at a slant, along it to the
  gable's end; sprinted at, over it. The belfry's beam — along it to its end; pressed to its side, stopped on it.
  Hanging on CasaCrema's impost band, pressed 6 s towards each end: still hanging at the ends.
* **The demo**: every shot in its time (the walk along the beam to its end, the perch, the leap into the hay, the
  climb up CasaCrema to its roof in 4.4 s).
* **`climb_check.gd`**: `leap_style` now checks the look (on the hands' place at the end of each of its 3 moves:
  0.0 cm); all 37 lanes value for value on GodotPhysics3D and Jolt against milestone 153.
* **Regression suite**: `climb`, `ledge_corners`, `traversal` exact.
* **The class reference**: `xmllint` and `make_rst.py` clean.
* **Pictures and video** (lavapipe, Movie Maker): the walk, run, turn, sprint and stop on the square from beside;
  the climb up CasaCrema from beside the climber.

## 3. Next

* The GPU, when the owner allows it: `f518`, then a fix for the closed frame's path (decision 0270).
* The climbing: the feet on the wall's own holds, a free hang under an eave, the fingers on the stone.
* The ground: the feet placed by IK on the roofs' slopes, starts and stops of their own, a reaction to landings.

Sources: [GDC13 Summary: Animation Bootcamp Part 4/6 (Wolfire)](https://wolfire-blog.appspot.com/2013/04/GDC13-Summary-Animation-Bootcamp-Part-4-6),
[How Assassin's Creed III animation team raised the bar 10 years ago (WN Hub)](https://wnhub.io/news/art/item-577).
