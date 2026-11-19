# FellowEngine — milestone 155 report (legs that bear weight; the showcase's animation measured in a loop)

Status: **FINAL** (2026-11-19). Branch `fellow/geometry`. Everything here ran on the CPU and lavapipe in the cloud: the
laptop's GPU stays paused at the owner's request (`f518` held in `.tools/wip`).

## 0. Result

The owner, of milestone 154's video: "these over-straightened legs are supposed to be realistic? For heaven's sake, plan
a workflow for improving this with a quality loop."

* **The loop** (`docs/showcase_animation_quality.md`): reference bands from gait analysis; a measuring rig in the
  repository (`showcase/tools/anim_quality.gd` and `.py`: the bones after the modifiers through fixed programs, 32
  gates, a baseline build to compare with, plots of the joints over the stride); one cause at a time, rebuilt and
  measured; an iteration that does not move its gate or breaks another undone; pictures judged only once the gates
  pass; the owner's remarks become gates. 17 iterations in this milestone.
* **The legs, measured** (milestone 154): the walking knee within 5° of straight in 57 % of the frames (its median
  3.6°: the IK's limit), the running 31 %, the sprinting 35 %, the mantle 25 %. The cause: the clips' generator asked
  the stance ankle to go 1.0 m from the hip on a 0.86 m leg.
* **Now**: straight knees 0 % in every gait and in the mantle; the walking knee 32.5° as the toe leaves (3.6°) and
  69° in the swing; the hip 34.6° at the heel's strike; the feet no longer in the ground (-2.2 cm → -0.8 cm); a planted
  foot moves 0.006 cm in a walking stance (2.3 cm); dragged 2.2-2.3 cm through a change of gait (7.7-20.6 cm). 25 of
  the 32 gates pass (14).
* **Decision 0295**:
  * the gaits rebuilt from the supporting leg (the hips as high as the stance legs reach at a knee curve, never higher
    than every planted foot can be reached; the heel raised as the knee needs; a swing that leaves and reaches the
    ground at its speed; the pivots the turned foot's own, the ball's on the sole; a jog between the walk and the run;
    the clips at 60 Hz) and soft IK in the generator;
  * `FellowFootLock3D` (engine, a new node): each foot held where its sole comes down, only its slide taken out, by
    two-bone IK — the blend of two gaits, a turn, a start or a stop no longer drags it;
  * `FellowTraversalPose3D.soft_ik` (off by default; the showcase 0.97): the mantle's legs no longer straight.

## 1. Checks (cloud)

* **`anim_quality.py`**: the gate table in decision 0295; 25/32 (milestone 154: 14). Still failing: the walking knee
  after the strike (20.6°, band 5-20°) and the ankle after it (8.9°, band 10-25°), the running and sprinting knees'
  least flexion at the end of the push (14.7°, 12.8°; band ≥ 15°), the running and sprinting skate (2.3, 3.1 cm; ≤ 2:
  the foot lands still moving forwards), the walk-to-run change's fastest joint (1.71 × the gaits'; ≤ 1.5).
* **`climb_check.gd`**: the 37 lanes' events, places and times value for value on GodotPhysics3D and Jolt; the last
  ledge check's diagnosis of two or three free-climb lanes swapped among them with the new class registered (and not
  with it unregistered, the same build otherwise): registering a class moves object ids, and the free climb's last
  check depends on their order.
* **Regression suite**: `climb`, `ledge_corners`, `traversal` exact.
* **The class reference**: `FellowFootLock3D` and `soft_ik` documented, in `--doctool`'s order; `xmllint` and
  `make_rst.py` clean.
* **The demo**: every shot in its time (its walk onto the beam now follows its path within 0.2 m).
* **Pictures and video** (lavapipe): the walk and the run from beside — the knees bent under the weight, the heel
  striking and the foot rolling onto the ball, the swing's knee high; the climb up CasaCrema.

## 2. Next

* The loop's failing gates, in order: the running foot's landing speed, the running knee at the end of the push, the
  walking knee and ankle after the strike, the walk-to-run change.
* Then, each with its gates first: the feet on slopes (the roofs), starts and stops, landings; the climbing against
  stills from Assassin's Creed chosen by the owner.
* The GPU, when the owner allows it: `f518`, then a fix for the closed frame's path (decision 0270).
