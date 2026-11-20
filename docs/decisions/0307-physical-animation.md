# 0307 — Physical animation: the bones as bodies that follow the animation, give way to hits, fall as a ragdoll

Status: **KEEP** (a new node: nothing changes without it) · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_physical_animation_3d.{h,cpp}`: `FellowPhysicalAnimation3D`, a `SkeletonModifier3D`;
* `benchmarks/project/bench/physical_check.gd`, `bench/physical_video.gd`.

Checks: `physical_check.gd` on Godot Physics and on Jolt Physics, walking and running; a character filmed on lavapipe.

## Problem

After decisions 0305 and 0306 the motion matcher's animation is grounded: its feet are planted, its body leans and its
hips turn. But it is still an animation: nothing in the world can touch it. AAA characters flinch when hit and recover,
and fall as bodies when they die (Euphoria-like physical animation, Unreal's physical animation component). FellowEngine
had the stock `PhysicalBoneSimulator3D`, which is all or nothing: the animation, or a ragdoll.

## Change

`FellowPhysicalAnimation3D`, placed on the skeleton after the modifiers that animate it.

**The bodies.** It builds the skeleton's bodies at run time on the stock simulator, as the editor's "Create Physical
Skeleton" makes them, by code:

* a capsule along each bone at least 8 cm long;
* the trunk (the chain from the root to the head's end, below 80 % of its height) as thick as a torso;
* a sphere for the root;
* a small body for every bone between two bodies;
* masses by volume, 70 kg together.

The CMU skeleton gets 21 bodies.

**The drive**, every physics step:

* Each body's velocity goes towards the animation's own motion since the last step, plus half its remaining error,
  less the gravity the step will add. Angles are driven the same way.
* The share of the step it takes is the strength cubed.
* Velocities are read from the physics server's state, so a hit's impulse is in them.

**Joints.** While driven, the joints are pins: a limit about one pose would fight every other pose. Once the strength is
under 0.5 they are made cones, about the pose it went limp in. Joints are always made with the bodies where the
animation has them, so their anchors agree.

**Hits.** `apply_hit()` puts the impulse into the nearest body. That body is weakened by 0.85, its neighbours by half as
much a link (two links), and the weakness fades with a 0.3 s halflife.

**A ragdoll** is strength 0.

**Safety.** The bodies start at the first animated pose (the skeleton's rest pose had the CMU legs under the ground).
A driven body held more than 0.75 m from its bone is put back on it.

**Warnings.** The skeleton must update its modifiers in the physics step, and a ragdoll wants Jolt Physics.

## Found on the way (all fixed before the measurements)

* Setting a physical bone's joint rotation puts the body back at its rest pose: it is now set once, when the bodies are
  built.
* Joints made before the bodies were placed had anchors 0.5 m apart.
* The node's cached velocity had dropped a hit's impulse.
* Cone limits about the standing pose threw the legs a metre while walking.
* `SkeletonModifier3D.active` was already taken: the node's own switch is `simulate`.

## Measurements

`physical_check.gd`: one character walking (1.3 m/s) on the CMU database, then a 40 N·s push at the chest from the
side and a 20 N·s push at the head from the front, then strength 0 for 4 s.

| | Godot Physics | Jolt Physics | Jolt, running 3 m/s |
|---|---:|---:|---:|
| bodies' distance from the animation, mean · largest | 3.0 · 28 mm | 3.6 · 32 mm | 8.0 · 37 mm |
| their angle from it, mean | 0.6° | 0.9° | 1.8° |
| chest push: chest knocked · hips moved · all back within 3 cm | 21 mm · 19 mm · 0.18 s | 102 mm · 10 mm · 0.27 s | 116 mm · 17 mm · 0.30 s |
| head push: head knocked · back | 222 mm · 0.28 s | 212 mm · 0.32 s | 210 mm · 0.42 s |
| ragdoll: fastest · at the end · into the floor | 37.7 m/s · 2.7 m/s · 75 mm (it jitters) | 4.6 m/s · 0.03 m/s · 46 mm | 5.4 m/s · 0.05 m/s · 46 mm |
| the drive a step | 12 µs | 16 µs | 13 µs |
| the physics frame, without → with the bodies (cloud CPU, one character) | 38 → 196 µs | 52 → 201 µs | 35 → 210 µs |

* With nothing pushing, the bodies follow the walk within a few millimetres: the character looks as the animation does.
* A hit knocks the struck part a hand's width, the hips barely move (the character keeps its feet), and every body is
  back within 3 cm in a third of a second.
* On Jolt the ragdoll falls at a fall's speed (4-5 m/s from standing) and lies still. On Godot Physics its joints jitter:
  the node warns.
* About 0.16 ms of physics a character a step on the cloud's CPU: for the characters near the camera, not crowds.

`physical_video.gd` films it (Jolt): walking, four pushes, then a ragdoll.

## Decision

KEEP: a node of its own, off unless a scene has it.

## Rejected / not done

* A pose-matching PD on torques (a motor per joint): the velocity drive is unconditionally stable at 60 Hz, and the
  bodies keep the animation's own motion as feed-forward.
* Driving with cone joints' limits about the rest pose: they threw the legs while walking.
* Getting up after a fall (blending from the ragdoll's pose into a get-up clip), the struck character's stagger in its
  locomotion (a step taken to keep its balance), and LOD (the bodies only for characters near the camera): next.
* Godot Physics' ragdoll jitter: the engine's, not the node's (warned).
