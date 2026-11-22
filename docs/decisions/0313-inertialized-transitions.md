# 0313 — Inertialized animation transitions: FellowInertializer3D

Status: **KEEP**, a new node (nothing changes without it) · Branch `fellow/geometry` · Node `FellowInertializer3D`
(`modules/fellow_animation/fellow_inertializer_3d.*`, a `SkeletonModifier3D`) · Measured with
`benchmarks/tools/animation_transitions/` (new).

Checks: the cloud's CPU, headless, 128 CMU locomotion clips; no GPU.

## Problem

Godot blends animation transitions by cross-fading: `AnimationPlayer.play(custom_blend)`, `AnimationTree`'s
transitions and blend nodes evaluate both animations for the whole fade and mix their poses with a weight. Mixing two
poses whose feet are in different places slides the feet, and the weight's ramp jolts the bones' speed at the fade's
start and end. AAA engines moved to inertialization (Bollo, Gears of War 4, GDC 2016; Unreal's Inertialization node):
switch at once, evaluate only the new animation, and fade out the difference to the old pose. Holden's "dead blending"
(2023) does it without the new animation's velocity: the old pose is carried on by its own velocity, slowing, and
cross-faded to the new one.

## Change

`FellowInertializer3D`, a skeleton modifier after the animation: the animation switches at once (no cross-fade) and the
node blends over `blend_time` (0.25 s) from the last pose drawn — each bone carried on by the velocity it had (measured
from the last two poses drawn, when the blend starts), the velocities halving every `extrapolation_halflife` (0.05 s)
— to the new animation's pose, by a smoothstep. Blends start on `blend()`, on the `animation_started` of the
AnimationPlayer or AnimationTree at `animation_mixer_path`, or (`detect_jumps`, off) when a bone turns more than
`jump_angle` (20°) further in a frame than its velocity carries it. A blend started during another starts from where
that one had got, with its velocity. The skeleton may run its modifiers more than once a frame, the later times with no
time passed: the blend is applied again as it stands, no velocity measured over no time (the first version measured
zero velocities there and carried nothing on).

## Measurements

The transitions course (new): CMU's locomotion (subjects 16, 35, 69: 128 clips), a clip switched every second to a
random one at a random time, 120 switches; the same sequence on characters side by side, root motion applied:
`ref` switches at once (which feet the new animation has planted: slower than 0.15 m/s), `xfade` cross-fades
(AnimationPlayer), `dead` switches at once under a FellowInertializer3D. Over each transition (the switch and the blend
after it): the planted feet's speed (sliding), the largest bone turn in a frame, the bones' largest angular
acceleration in a frame, and the CPU time of `AnimationPlayer.advance()` and of the modifier.

| blend 0.25 s | sliding (m/s) | turn in a frame p95 / max | angular acceleration p50 / p95 (deg/s²) | player + modifier (µs a frame) |
|---|---:|---:|---:|---:|
| switch at once (`ref`) | 0.08 | 27.9° / 180° | 14 935 / 150 606 | 8.2 |
| cross-fade | 0.97 | 9.2° / 43.6° | 12 006 / 38 356 | 8.8 |
| **FellowInertializer3D** | **0.79** | **6.8° / 22.8°** | 13 239 / **30 268** | 5.8 + 2.7 |

| blend 0.4 s | sliding | turn p95 / max | acceleration p50 / p95 |
|---|---:|---:|---:|
| cross-fade | 0.86 | 8.0° / 57.8° | 11 694 / 36 543 |
| FellowInertializer3D | 0.68 | 5.2° / 33.9° | 10 178 / 25 940 |

* Against the cross-fade: the feet slide 18-21 % less, the largest turn in a frame halves, the acceleration's 95th
  percentile is 21-29 % lower; the player evaluates one animation instead of two (−34 %), the modifier adds back
  2.7-3.8 µs for 31 bones: the same total here, less where animations have more tracks than the skeleton has bones.
* Blends started by the player's `animation_started` (`animation_mixer_path`) give the same results as by hand.
* The extrapolation's half-life: 0.05 s slides 0.79 m/s, 0.1 s 0.83, 0.2 s 0.86; holding the old pose still (0.001 s)
  0.74, the acceleration's 95th percentile the same (30 037). 0.05 s kept (the old motion not stopped dead at the
  switch; Holden's value), smaller values for less sliding.
* At 0.15 s both slide alike (1.23 m/s): too short for either to hide the change of feet.
* Every blend slides: the old pose's feet are where the old animation had them, the character moves on with the new
  one's root motion. Foot locking (`FellowFootLock3D`, decision 0295) is the usual remedy, with either (not measured here).

## Decision

KEEP: a node for transitions in place of cross-fades. Not a change to `AnimationTree`'s own blending (stock's API and
results unchanged).

## Rejected / not done

* Inertialization with the new animation's velocity (Bollo's quintic over the offset): needs the new animation sampled
  at two times at the switch, which a modifier after the mixer does not see; dead blending needs only the pose.
* A blend node inside `AnimationTree`: a deep change to the mixer's blending for one technique; the modifier works
  with AnimationPlayer, AnimationTree and code alike.
