# FellowEngine — milestone 166 report (physical animation)

Status: **FINAL** (2026-11-21). Branch `fellow/geometry`. Everything ran in the cloud (CPU; the film on lavapipe); the
laptop's GPU stays paused.

## 0. Result

The owner: "go on to AAA". After the grounded locomotion of milestones 164-165, the next AAA layer of animation is
physical animation: characters that flinch when hit and fall as bodies (decision 0307).

`FellowPhysicalAnimation3D` is a new node, placed on a skeleton after the modifiers that animate it. It builds the
bones' rigid bodies at run time, 21 for the CMU skeleton, and drives them every physics step towards the animation:

* with nothing pushing, the bodies follow a walk within 3-4 mm, so the character looks as its animation does;
* a hit knocks the struck part and weakens it and its neighbours for a moment: a 40 N·s push at the chest knocks it
  10 cm, one of 20 N·s at the head 21 cm. The hips barely move, and every body is back within 3 cm in 0.3 s;
* at strength 0 it is a ragdoll: on Jolt Physics it falls at a fall's speed and lies still.

## 1. Checks (cloud)

* `bench/physical_check.gd`, on Godot Physics and on Jolt Physics, walking and running (decision 0307's table).
* The ragdoll on Godot Physics jitters (its joints); the node warns when the engine is not Jolt. New projects use Jolt.
* `bench/physical_video.gd`: a character walking, pushed four times, then let go (`physical_m166.mp4`).
* The cost: about 0.16 ms of physics a character a step on the cloud's CPU. It is for the characters near the camera.
* The class reference validates and matches the engine's.
* The laptop: `.tools\bundles\m166.bundle`; `Showcase - Dachy.bat` builds the engine as `.tools\bin\m166e`.

## 2. Next (animation)

* Getting up: from the ragdoll's pose into a get-up clip, by motion matching.
* A stagger: a pushed character's locomotion taking a step to keep its balance.
* Physical animation's LOD: the bodies only near the camera, faded in and out.
* The showcase's player on motion matching with physical animation.
