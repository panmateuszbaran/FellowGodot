# 0293 — The eyes on the hold: where the climber looks; the head turned to it

Status: **KEEP** (the hint changes no pose by itself; the showcase turns the head with Godot's `LookAtModifier3D`) ·
Branch `fellow/geometry` · Code: `modules/fellow_animation/fellow_traversal_3d.{h,cpp}` (`PoseHints::look`,
`look_weight`; `_step_transfer()`, `_step_hang()`, `_update_hints()`, `get_pose_targets()`); the showcase's player
(`tools/build_rooftops.gd`: `HeadLook`, `LookTarget`; `scripts/rooftops_player.gd`: `_process()`) · Checks:
`bench/climb_check.gd` (`leap_style`: the look on the next hold), the other 36 lanes, the showcase's climb probed.

## Problem

The owner asked to compare the climbing with Assassin's Creed's. There (AC III's talk at GDC 2013: the hands and feet
placed by full-body IK, the whole body reacting to each jump and landing) the climber looks where it goes: at the next
hold before it leaps, along the ledge the way it shimmies, over the top as it climbs up; running, the head turns into
a turn before the body. The showcase's climber looked straight ahead into the wall whatever it did: the head was the
clips', which know nothing of the next hold.

## Change

* **`look`, `look_weight`** in the traversal's pose hints (`get_pose_targets()`): moving from hold to hold, the next
  hold (where the hands will be; from the move's start — a leap's wind-up included); hanging, the hold, along the
  ledge the way the controller wishes to go (up to 0.9 m) and up the wall when it wishes to climb (up to 0.7 m);
  through a mantle, over the top; nothing on a ladder or a free climb.
* **The showcase**: a `LookAtModifier3D` on the mannequin's head after the climbing's pose (relative to the animated
  head, ±55° to the sides, ±35° up and down, eased at the limits); the player moves its target in the idle frames — the
  traversal's look on a wall, the way the controller steers on the ground (ahead of the body through a turn), the way
  it faces standing — and eases the way (0.11 s) and the weight (0.17 s; 1 on a wall, 0.85 running, 0.5 standing, 0 in
  the air).

## Checks

* **`climb_check.gd`**: `leap_style` — through each of its three moves from hold to hold the look is on the hands'
  place at the move's end (0.0 cm); the other 36 lanes value for value on GodotPhysics3D and Jolt.
* **The showcase's climb up CasaCrema** (the bones read after the modifiers): the head turned 15-70° from the body
  towards the next hold through each reach across and leap, the face within 1-40° of the look (the limits and the
  easing), straight ahead again on the roof.

## Decision

KEEP.

## Rejected / not done

* **A look in `FellowTraversalPose3D`** (a head role turned by the modifier): Godot's `LookAtModifier3D` turns a bone
  to a target with limits and easing already; the traversal says where to look, a game says how.
* **The eyes on a free climb's next hold**: the free climb has no next hold (it moves over the wall's surface).
