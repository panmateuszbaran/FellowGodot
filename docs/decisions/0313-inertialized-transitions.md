# 0313 — Inertialized animation transitions: FellowInertializer3D

Status: **KEEP**, a new node (nothing changes without it) · Branch `fellow/geometry` · Node `FellowInertializer3D`
(`modules/fellow_animation/fellow_inertializer_3d.*`, a `SkeletonModifier3D`) · Measured with
`benchmarks/tools/animation_transitions/` (new).

Checks: the cloud's CPU, headless, 128 CMU locomotion clips; no GPU. Measurements corrected in milestone 172.

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

Corrected in milestone 172 (below: what was wrong). The transitions course (new): CMU's locomotion (subjects 16, 35,
69: 128 clips), a clip switched every second to a random one at a random time, 120 switches; the same sequence on
characters side by side, moved by their root motion — a game rig's root bone made from the mocap's hips (on the ground
under them, turned with their heading), the hips' height, sway and tilt left to them. `ref` switches at once (which
feet the new animation has planted: slower than 0.15 m/s), `xfade` cross-fades (AnimationPlayer), `dead` switches at
once under a FellowInertializer3D. Over each transition (the switch and the blend after it), once a frame from the pose
drawn: the planted feet's speed (sliding), the largest bone turn in a frame, the bones' largest angular acceleration in
a frame, and the CPU time of `AnimationPlayer.advance()` and of the modifier.

| blend 0.25 s | sliding (m/s) | turn in a frame p95 / max | angular acceleration p50 / p95 (deg/s²) | player + modifier (µs a frame) |
|---|---:|---:|---:|---:|
| switch at once (`ref`) | 0.08 | 27.9° / 180° | 14 935 / 150 606 | 9.2 |
| cross-fade | 1.06 | 9.2° / 43.6° | 12 006 / 38 356 | 9.2 |
| **FellowInertializer3D** | **0.95** | **8.7°** / 50.3° | **7 034 / 31 771** | 6.2 + 2.8 |

| blend 0.4 s | sliding | turn p95 / max | acceleration p50 / p95 |
|---|---:|---:|---:|
| cross-fade | 0.82 | 8.0° / 57.8° | 11 694 / 36 543 |
| FellowInertializer3D | 0.73 | 7.3° / 57.3° | 6 263 / 29 865 |

* Against the cross-fade: the bones' acceleration's median is 41-46 % lower and its 95th percentile 17-18 % (the
  smoothness), the feet slide 10-11 % less, the turn in a frame's 95th percentile 6-9 % less; the largest single turn
  is no better (50° against 44° at 0.25 s). The player evaluates one animation instead of two (−33 to −45 %), the
  modifier adds back 2.8-3.6 µs for 31 bones: about the same total here, less where animations have more tracks than
  the skeleton has bones.
* Blends started by the player's `animation_started` (`animation_mixer_path`) give the same results as by hand.
* The extrapolation's half-life at 0.25 s: 0.05 s slides 0.95 m/s, 0.1 s 0.99; holding the old pose still (0.001 s)
  0.90, the acceleration alike, its largest turn 78°. 0.05 s kept (the old motion not stopped dead at the switch;
  Holden's value).
* Every blend slides: the old pose's feet are where the old animation had them, the character moves on with the new
  one's root motion. With a foot lock after it (decision 0314) the feet slide 26 % less than with the cross-fade alone.

**What was wrong at first (milestone 171).** The course measured a character's motion at each of its skeleton's
updates, and a skeleton with modifiers updates twice in a frame (the second time with nothing changed): the
inertialized character's frames were half measured as standing still. Its sliding (0.79 m/s), its turns (6.8° / 22.8°)
and the advantages drawn from them (−18 % sliding, the largest turn halved) were too good. The course now keeps the
last pose drawn and measures once a frame; the cross-fade's and the reference's numbers did not change.

## Decision

KEEP: a node for transitions in place of cross-fades. Not a change to `AnimationTree`'s own blending (stock's API and
results unchanged).

## Rejected / not done

* Inertialization with the new animation's velocity (Bollo's quintic over the offset): needs the new animation sampled
  at two times at the switch, which a modifier after the mixer does not see; dead blending needs only the pose.
* A blend node inside `AnimationTree`: a deep change to the mixer's blending for one technique; the modifier works
  with AnimationPlayer, AnimationTree and code alike.
