# 0180 — Mantling and vaulting: a ledge found with physics queries, the body moved by a path, the pose procedural or a clip

Status: **KEEP** (new nodes: nothing changes without them) · Branch `fellow/geometry` · Code:
`modules/fellow_animation/fellow_traversal_3d.{h,cpp}` (`FellowTraversal3D`: the checks, the path, the pose's targets,
the counters), `modules/fellow_animation/fellow_traversal_pose_3d.{h,cpp}` (`FellowTraversalPose3D`: the pose on the
skeleton), `modules/fellow_animation/register_types.cpp` (the monitor), `modules/fellow_editor/fellow_editor_plugin.cpp`
(the gizmo, the "Add Mantling Character" setup) · Checks: `benchmarks/project/bench/traversal_check.gd` (the course,
`--crowd=N`), the regression scene `traversal`, the editor check; the laptop (job f390).

## Problem

The owner pointed at a Godot project's mantling (a character pulling itself onto ledges of any height it can reach,
in one motion with its hands on the edge) and asked for the same in the engine: procedural, with places for clips.
Godot has the pieces (`PhysicsDirectSpaceState3D`'s queries, `CharacterBody3D`, skeleton modifiers, two-bone IK) but no
traversal: every game writes its own ledge detection and its own mantle, usually with a clip per height that fits only
the heights it was made for.

## Change

**`FellowTraversal3D`** (a child of the body: a `CharacterBody3D`, or any `CollisionObject3D` at `body_path`). Its
capsule is read from the body's first `CollisionShape3D` (a capsule, cylinder, box or sphere). Each physics frame
while the body moves (`auto_min_speed`, or the controller's `set_intent()`: a body pressed against a wall has lost its
velocity into it to the slide), or on `try_mantle()`, `find_ledge()`:

1. A ceiling over the body bounds the heights searched (a ray up).
2. **A wall**: a capsule spanning the heights a ledge may have (`min_height` to `max_height` over the feet, 0.5 m more
   in the air with `grab_in_air`) cast along the way the body goes by `reach_distance` (`cast_motion`, then
   `rest_info` where it stops: Jolt's `cast_motion` gives no contact); the contact's normal must be a wall's (mostly
   horizontal) facing the way within `max_wall_angle`.
3. **A top**: a ray down just inside the face from above the highest ledge (`hit_from_inside`: a ray starting in the
   wall means there is no top: `KIND_TOO_HIGH`); its height over the feet decides `KIND_STEP` (under `min_height`),
   `KIND_LOW` (to `low_max_height`), `KIND_HIGH`; a top steeper than `max_ledge_slope` is blocked. The edge is found
   again by a ray at the top's height (a leaning wall's face moves with the height).
4. **A thin obstacle**: a ray down beyond `vault_max_depth` finds the far side 0.25 m or more below the top, a ray back
   finds its far face (the depth), and a landing beyond it.
5. **Room**: the body's capsule (5 % thinner, so that touching is not blocking: `intersect_shape`) on the top beyond
   the edge (the same top: within 0.25 m of its height), straight up in front of the wall at the top's height, over
   the edge, and for a vault over the middle and on the landing. A thin obstacle is vaulted when there is no room on
   it or the body runs at `vault_min_speed`; otherwise no room is `KIND_BLOCKED` with the reason.
6. **Passable**: a ledge the body could walk on under (its capsule cast along the way, free: a beam over head height)
   is left to `try_mantle()`.

The mantle moves the body itself (its global transform each physics frame, as the body's child after the body's own
physics processing; a `CharacterBody3D`'s velocity follows the motion), along a path from the ledge: toward the face
(no closer than the capsule's radius) while rising to 4 cm over the top, then onto it (`KIND_LOW` turns at 55 % of the
time, `KIND_HIGH` at 62 %); a vault rises to the top in the first third, crosses, and falls to the landing once the
capsule is past the far face. The capsule therefore never enters the wall: the course's mantles overlap the world in
no frame. Durations: `low_duration` (0.6 s for 1 m) and `high_duration` (1.1 s for 1.8 m) scaled with the square root
of the height, `vault_duration`. `align_to_wall` turns the body to face the wall in the first 30 %. A collider that
moves carries the ledge (its transform's change since the check applied to the edge and the target). At the end the
body leaves at `exit_speed` into the ledge (a vault keeps its speed). Signals `mantle_started(kind, ledge)`,
`mantle_finished`, `mantle_cancelled` for an `AnimationTree`.

**Clips** (`low_clip`, `high_clip`, `vault_clip`): the mantle lasts the clip's length; with `clip_root_bone` the clip's
root motion (that bone's position track, sampled at 60 Hz) is the path, its rise scaled to the ledge's height (a vault:
its highest point to the top) and its advance along its own horizontal direction scaled to the distance onto the top
(or the landing), kept from the face until the root has risen (a clip that leans in early, scaled onto a higher ledge,
would put the capsule in the wall). The clip plays at the mantle's time on the skeleton, the root bone's displacement
since the clip's start taken out of its pose.

**The pose's targets** (`get_pose_targets()`, global space, per physics frame): the hands on the edge 0.22 m either
side of the body's line (a vault: one hand on the top), the feet on the wall's face, then onto the top (`KIND_LOW`: one
foot steps up, the other against the wall; `KIND_HIGH`: both against the wall while pulling up, then one onto the top;
a vault: both tucked), the torso's lean towards the wall, each with a weight that rises and falls over the mantle's
progress, and how far the hips may hang below the capsule's place (no more than the body has risen, none once it moves
over the top).

**`FellowTraversalPose3D`** (a modifier of the character's `Skeleton3D`, after its animation): finds the traversal among
the children of the skeleton's ancestors, finds its bones by name (Godot's humanoid profile, Mixamo, Unreal's
mannequin, Blender's `.L`/`.R`; `bone_names` for others) and, while a mantle runs, over whatever the animation plays:
leans the spine and chest; lowers the hips until each shoulder is within 97 % of its arm's reach of its hand's target
(the hands reach the edge while the body hangs and pushes up: the capsule must be over the top before it moves onto
it, which leaves a standing body's shoulders 1.4 m over the hands); places the feet (two-bone IK, the knees towards the
wall, each foot keeping its turn in the world; the wall's feet lowered with the hips) and the hands (two-bone IK, the
elbows out, down and back). Blended in and out over `blend_time` (a clip's last pose held while it fades). A clip set
on the traversal is played instead.

`FellowTraversal3D.get_global_stats()` counts every traversal's checks, mantles, poses and their time; the debugger's
Monitors tab shows "FellowEngine/Traversal time" a frame. `debug_draw` draws the last ledge (green: climbable, red: not)
and the path. The editor draws the heights searched in front of the body (its capsule's feet) and the FellowEngine
menu adds a body with a capsule and a traversal.

## Checks (cloud: lavapipe and the CPU)

`bench/traversal_check.gd`: nineteen runners at 1.5-4.5 m/s, one obstacle each, the test mannequin on each skeleton:

| lane | expected | GodotPhysics3D | Jolt |
|---|---|---|---|
| a 0.3 m step | nothing | nothing | nothing |
| ledges of 0.9 m, 1.25 m | low, on the top | low (35, 41 frames), on the top | the same |
| a 2.0 m ledge | high, on the top | high (70 frames), on the top | the same |
| a 2.6 m wall | too high | too high | too high |
| a 1 m ledge under a ceiling 1.3 m over it | blocked | blocked (no room on top) | the same |
| a ceiling over the runner (a 0.3 m slab at 2 m), a 1 m ledge under its end | blocked | the slab passed under (passable), the ledge blocked (no room above) | the same |
| a thin wall (1 m high, 0.25 m deep) at 4.5 and 1.5 m/s | vault | vault (37 frames), landed 0.65 m beyond | the same |
| a thin wall with a wall 0.4 m behind | blocked | blocked (no room) | the same |
| walls turned 30° and 60° | low; nothing | low, turned 30°; slid along | the same |
| a cylinder (r 0.9 m, 1.1 m) | low | low | the same |
| a 12° ramp, a top sloped 50° | nothing; blocked | nothing; blocked (slope) | the same |
| a moving platform (0.8 m/s) | low, on it | low, on it 2.8 m later | the same |
| a jump to a 2.9 m ledge (too high from the ground) | high from the air | high (58 frames), on the top | the same |
| a 1.2 m ledge with a clip (root motion up 1 m, forward 0.8 m) | low in the clip's time | low (60 frames: 1 s), on the top; halfway the spine at the clip's 34°, the hips' root motion out (0 offset) | the same |
| the traversal disabled | nothing | nothing | nothing |

Every mantle: the capsule (a 0.31 m test capsule inside the 0.35 m body) overlaps the world in no frame; the hands and
feet reach their targets within 0.05 cm whenever they are within reach (the IK's error), the hips lowered by up to
0.6 m (low), 0.9 m (high), 0.45 m (vault). The same decisions with Jolt, the bodies within 2 cm of
where they end with GodotPhysics3D (with `check_interval` a mantle may start a frame apart).

`find_ledge()` on the cloud's CPU (editor build, 2 000 calls each): nothing in front 4.4 µs (Jolt 2.6), a wall too high
11.7 µs (7.0), a thin wall 19.5 µs (14.6), a ledge 26 µs (20).

The pictures (lavapipe): a strip of six mantles at six moments and a video: the hands on the edge, the hips hanging,
a foot on the wall's face, the knee onto the top, the vault's tucked legs. The regression scene `traversal` holds three
mantles partway (each traversal stopped at a set progress): the same picture every run.

`check_interval` (2): while nothing is in reach, the automatic check runs every second physics frame (staggered
between traversals; with a wall in reach, every frame). In the crowd below (the cloud's editor build, 200 runners,
10 s): the checks a frame 109 → 56, their time 0.46 → 0.25 ms (GodotPhysics3D) and 0.26 → 0.15 ms (Jolt), the same
mantles (88 a second); the course's decisions the same (two lanes start their mantle a frame apart).

## Results on the laptop (job f390, release template)

The course with GodotPhysics3D and with Jolt: every lane as on the cloud (the same frames, positions and IK errors).
`find_ledge()`: nothing in front 4.5 µs (Jolt 2.6), a wall too high 11.0 µs (5.8), a thin wall 19.7 µs (12.4), a ledge
27.6 µs (12.2) — a GDScript call with its result's dictionary included.

**Crowds** (`--crowd=N`, headless: the CPU's whole frame; each runner up and down a lane of a 0.9 m box, a thin wall, a
1.25 m ledge and a 1.8 m ledge at 3.5 m/s, its skeleton posed by a `FellowTraversalPose3D`; 20 s, two runs each):

| | 50 runners, GodotPhysics3D | 50, Jolt | 200, GodotPhysics3D | 200, Jolt |
|---|---|---|---|---|
| mantles and vaults a second | 22 | 22 | 89 | 89 |
| checks a frame (µs each) | 14 (4.7-5.0) | 14 (2.4-2.6) | 56 (4.6-4.8) | 56 (2.4-2.5) |
| the traversals' time a frame | 0.121 / 0.125 ms | 0.077 / 0.082 ms | 0.474 / 0.503 ms | 0.330 / 0.319 ms |
| of it: checks / mantles / poses | 0.066 / 0.009 / 0.047 ms | 0.034 / 0.007 / 0.036 ms | 0.254 / 0.035 / 0.184 ms | 0.137 / 0.033 / 0.159 ms |
| the frame (physics, scripts, skeletons) | 0.70 / 0.72 ms | 0.50 / 0.52 ms | 2.46 / 2.59 ms | 1.89 / 1.81 ms |

About 2.4 µs a character a frame with GodotPhysics3D and 1.6 µs with Jolt, in a crowd where half the runners are
mid-mantle at any time; a pose 2 µs. The six mantles filmed on the RTX (Forward+, `--write-movie`): the same motion as
lavapipe's.

## Decision

Keep both nodes. A ledge is decided by geometry, not by tags or volumes placed by hand, so it works on any collision
(boxes, cylinders, convex shapes, moving platforms); the capsule never enters the world because the path is built
from the room the checks found; the pose fits any height because it is IK over the character's own animation, and a
clip, where a project has one, is warped onto the ledge instead of being chosen for it.

## Rejected / not done

* **Clips chosen per height, unwarped** (the common approach): the hands land at the clip's ledge, not this one; a clip
  with root motion warped to the ledge does what a set of clips would, and the procedural pose needs none.
* **Moving the body with `move_and_collide`**: the path is through room already checked; the physics step would push
  the capsule off the edge it is rising past.
* **The hands' contact timing from a clip** (IK onto the edge while a clip plays): a clip's own hands are near its own
  ledge; matching its contacts needs per-clip markers — next, with a clip set made for it.
* **Climbing walls, hanging and shimmying along ledges, ladders**: other traversals; the checks and the pose's targets
  are the base for them.
