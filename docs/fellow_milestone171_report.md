# FellowEngine — milestone 171 report (inertialized animation transitions)

Status: **FINAL** (2026-11-22). Branch `fellow/geometry`. The cloud's CPU only, headless (no GPU, by the owner's
decision).

## 0. Result

Godot cross-fades animation transitions: both animations evaluated for the whole fade and their poses mixed, which
slides the feet and jolts the bones' speed at the fade's ends. AAA engines inertialize instead (Gears of War 4's
inertialization, Unreal's Inertialization node, Holden's dead blending). `FellowInertializer3D` (decision 0313), a
skeleton modifier: the animation switches at once and the node blends from the last pose drawn, carried on by its own
velocity, to the new animation's; only the new animation is evaluated. Blends start on `blend()`, on an AnimationPlayer's
or AnimationTree's `animation_started` (`animation_mixer_path`: no code), or on a detected jump in the pose.

The transitions course (new, `benchmarks/tools/animation_transitions/`): 120 random switches between 128 CMU locomotion
clips, root motion applied, the same sequence cross-faded and inertialized side by side:

| | cross-fade 0.25 s | FellowInertializer3D 0.25 s |
|---|---:|---:|
| the feet planted in the new animation, sliding | 0.97 m/s | **0.79 m/s** (−18 %) |
| the largest bone turn in a frame, 95th percentile / max | 9.2° / 43.6° | **6.8° / 22.8°** |
| the bones' angular acceleration, 95th percentile | 38 356 deg/s² | **30 268** (−21 %) |
| AnimationPlayer's time a frame (+ the modifier) | 8.8 µs | 5.8 µs + 2.7 µs |

At 0.4 s: sliding −21 %, the largest turn −41 %, the acceleration −29 %.

## 1. How it got there

* The skeleton runs its modifiers more than once a frame when an AnimationPlayer is advanced by hand (the second time
  with no time passed): the first version measured the velocities over no time there — zero — and carried nothing on
  (every extrapolation half-life gave the same results). The blend is now applied again as it stands on such calls.
* Velocities measured every frame cost 4.9 µs; now only the last two poses are kept and the velocities taken when a
  blend starts: 2.7 µs.
* The half-life: 0.05 s slides 0.79 m/s, 0.2 s 0.86; holding the old pose still 0.74 with the same acceleration. 0.05 s
  kept (Holden's), the property documented.

## 2. Checks

* The transitions course at 0.15, 0.25 and 0.4 s, half-lives 0.001-0.2 s; blends started by the player's signal the
  same as by hand.
* `clang-format`, the class reference (`xmllint`, `make_rst.py --dry-run`).

## 3. Next (without the GPU)

* Foot locking through transitions (`FellowFootLock3D` with the inertializer) on the same course.
* Being pushed by rigid bodies; the camera easing steps (`CharacterBody3D.get_last_step()`).
