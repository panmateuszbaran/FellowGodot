# FellowEngine — milestone 156 report (the owner's reference video: the mannequin, its run, its scramble up the tiles; no pops)

Status: **FINAL** (2026-11-19). Branch `fellow/geometry`. Everything here ran on the CPU and lavapipe in the cloud: the
laptop's GPU stays paused at the owner's request (`f518` held in `.tools/wip`).

## 0. Result

The owner, of the rooftops showcase: "I'd rather this scene looked more like this" (a 4-second video: a grey
articulated mannequin running across a stone square, up a house's front by its sills, over the eave and up the tiles on
hands and feet), and "don't finish until the character's model is identical to the one in the reference video and the
animations are as smooth and realistic as in it".

* **The model** (`showcase/tools/blender/mannequin.py`): rebuilt as the reference's artist's mannequin on the same
  skeleton — a breastplate (broad, flat pectorals), the abdomen and a pelvis cut like briefs, thin dark seams between
  them; a large egg of a head with a face's plane; a dark conical neck; the arms' grey domes in dark sockets; dark
  elbows, wrists and ankles; the knee a grey ball between dark rings; heavier thighs and calves; feet like shoes;
  mid-grey. Compared in Blender (Cycles) beside the reference's frames, front and back.
* **The run**: the trunk 19.7° forward (5.8° before; the reference some 20°), the forearms swinging in front of their own
  shoulders (the forward hand had been up by the face and across the chest), the hands half closed, the thumb along
  the fingers; compared cycle by cycle beside the reference's first 32 frames.
* **The roof**: a new clip and controller state — up a roof steeper than 15° on hands and feet (a lateral sequence, the
  trunk nearly along the slope, the head up); the demo scrambles up CasaCrema's tiles after the mantle, as the
  reference ends.
* **No pops** — the whole body's smoothness measured for the first time (the fastest joint each frame, the hips'
  acceleration) and each pop's cause found and fixed: the clips' rig swapping a limb from FK to IK halfway (the jump at
  a wall: an arm 42° in a frame), a mantle snapping from the braced pose to its clip (a knee 8863°/s), the body let go
  from the wall at once over the eave (0.65 m in two frames), an IK bend turning over (a forearm 53° about its axis), the
  foot lock easing a foot back from beyond the leg's reach (a knee bent 23° in a frame). The climbing's fastest joints
  now 1371-2087°/s; 38 of 38 gates pass (milestone 155: 29 of 32).
* **The city**: the houses of sandstone ashlar (Poly Haven's `sandstone_blocks_05`, CC0) where they had been plastered
  in colours, the shade lighter; the demo's house filmed as the reference is (beside the runner, below the climber,
  beside the body on the tiles).
* **Decision 0296**; the engine's part: `FellowTraversalPose3D.clip_blend`, `pole_follow`, `max_joint_speed` (off by
  default), a fix in `FellowTraversal3D`'s mantle with a clip's root motion and one in `FellowFootLock3D`.

## 1. Checks (cloud)

* **`anim_quality.py`**: 38 of 38 gates (six new: the climbing's segments' fastest joint ≤ 2200°/s, the mantle's hips'
  acceleration ≤ 400 m/s²); the table in decision 0296. The gaits' gates no worse than milestone 155's; its three
  failing ones in their bands (the walk's knee at the strike 20.6 → 14.9°, its ankle in the loading 8.9 → 13.5°, the
  sprint's knee at the toe-off 12.7 → 15.9°).
* **`climb_check.gd`**: the 37 lanes' events, places, times and errors value for value on GodotPhysics3D and Jolt.
* **`traversal_check.gd`**: the 19 lanes value for value (with the mantle by a clip's root motion).
* **Regression suite**: `climb`, `ledge_corners`, `traversal` exact.
* **The class reference**: the three members and the foot lock's way back documented, in `--doctool`'s order;
  `xmllint` and `make_rst.py` clean.
* **Pictures and video** (Cycles for the model; lavapipe for the scene): the model and the run beside the reference's
  frames; the scramble; the house's sequence from the demo with its new cameras (`rooftops_m156.mp4`).

## 2. Next

* Hanging under a hold that stands out (CasaCrema's pediments), the body nearer the wall and the knees drawn up as the
  reference's climber's; the windows' openings in the houses' collision (the feet on their sills).
* The shade lighter still and the roofs less colourful (`look_compare.py`: the climb's view 18.9 % dark, the
  reference's 9.4 %).
* The feet on the roofs' slopes when walked (not only crawled); starts and stops; landings — each with its gates first.
* The reference's run as gates (its lean, its arms' swing, its stride), from a pose estimate if the owner allows one.
* The GPU, when the owner allows it: `f518`, then a fix for the closed frame's path (decision 0270).
