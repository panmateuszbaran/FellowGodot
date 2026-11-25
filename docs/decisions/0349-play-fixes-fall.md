# 0349 — Four faults found playing: the jump after the climb, the fall, the fountain, W at the wall

Status: **KEEP** · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_traversal_3d.{h,cpp}`: `hang_side_dead_zone` — hanging, a wish pushing in at the wall
  or up whose part along the edge is under that share of its length goes straight in or up;
* `showcase/scripts/rooftops_player.gd`: a jump pressed while hanging or climbing the traversal's alone (the jump
  buffer emptied); the fall a blend (`_fall_tree`, `_fall_mix`: `FALL_FLAIL_FROM`, `FALL_FLAIL_FULL`, `FALL_READY`);
* `showcase/tools/blender/mannequin_anims.py`: `clip_fall` (ready to land), `clip_fall_flail` (new);
* `showcase/rooftops/rooftops.tscn`, `tools/build_rooftops.gd`: the fountain's column and bowl collide; the showcase's
  `hang_side_dead_zone` 0.5;
* `showcase/tools/update_clips.gd` (new): clips rebuilt in Blender put into the library without rebuilding the town;
  `climb_look.gd`'s programs `keys` and `fountain`.

## Problem

The owner, playing: "after standing up on the roof the character does a strange jump; the fall has a broken animation;
the fountain in the middle has no collision box; climbing, when I hold W and press Space the character sometimes goes
along the edge, left or right at random."

## What was found

* **The jump on the roof**: a press of Space sets the player's jump buffer (0.15 s: a jump pressed just before landing
  still jumps), and the buffer is counted down only on the ground. A press while hanging (to climb on) stayed in it
  through the climb onto the roof, and the first frame on the roof jumped the body (4.8 m/s up), 1.7 s after the
  press (`climb_look.gd --program=keys`: the stick at the wall, Space every 0.5 s while hanging).
* **W at the wall**: the traversal's intent is the stick's direction times the speed it asks for (a run's 4.4 m/s),
  and the hang's thresholds are absolute: along the edge over 0.5 jumps along it, under that shimmies at up to the
  shimmy's speed. With the camera only 7° off square to the wall, W read as 0.55 along. At 8° off (camera turned
  right) the body shimmied 1.0 m and never climbed; turned left it leapt 4 m along the edge to the next hold; at 20°
  1.6 m and 4 m. Which way depends on the camera, so it looked random.
* **The fall**: the mannequin's own clip (Mixamo's "Falling Idle" is a skydiver face down, decision 0337): the body
  upright and stiff in the air, the legs nearly straight under it (the feet 0.66-0.87 m under the hips, a person
  standing 0.85), one arm out behind — the same from a jump's top as off a roof 11 m up.
* **The fountain**: one convex octagon to the basin's rim (0.75 m); the bowl (to 1.86 m) and the column (to 2.7 m) had
  none.

## Change

* The player empties the jump buffer while the traversal has the body (a press there is the traversal's: a leap, a
  climb).
* `hang_side_dead_zone`: a wish pushing in or up whose part along the edge is under that share of its length loses
  that part. The showcase's 0.5 takes the stick within 30° of square as straight; a diagonal (45°) still goes along,
  shimmies and reaches across as before. The player's own shimmy blend reads the same.
* The fall: "fall" is the body in the air ready to land — the knees up and bent (one leg ahead), the arms out to the
  sides for balance, the trunk a little forward, the eyes down; "fall_flail" (new) the arms wheeling over the
  shoulders out of step, the legs pedalling, the trunk a little back. The player blends them (`fall_mix`, a Blend2 in
  place of the state's clip) from 0.45 to 1.1 s in the air, and back to ready (over 0.25 s) once the ground is within
  0.3 s at the speed it falls: a jump's descent stays ready, a fall off a roof flails and readies itself to land.
* The fountain: two cylinders on its body — the column (r 0.45, to 2.7 m) and the bowl (r 1.5, 1.44-1.86 m).
* `tools/update_clips.gd`: the clips named, from `build_mannequin.py <dir> <clips>`'s Mannequin.glb, into
  `mannequin_clips.res` (as `build_rooftops.gd` takes them, compressed); `--check=1` compares them (the unchanged fall
  rebuilt: 0.08° at most).

## Measurements (headless)

| | milestone 206 | **milestone 207** |
|---|---|---|
| W and Space at the wall, the camera 8° off (either way): along the edge; up to the eave | 1.0 m shimmied, or a 4 m leap along it; never | **0** (the route's own move across, 0.9 m); **yes**, as square |
| the same at 20°; at 45° (a diagonal) | 1.6 m, 4 m; along | 0; along (unchanged) |
| after the climb onto the roof, Space pressed while hanging | a jump, 4.8 m/s up, 1.7 s after the press | none |
| the fall off the roof (1.25 s): the knees bent, after 0.7 s; the hands | 11-76°; 0.08 m under the shoulders at most | **51-83°**; up to **0.31 m over** them |
| the fall: the fastest joint | 1,276 °/s (out of the run) | 1,276 °/s (the same), 374 °/s in the flail |
| the fountain: rays down at 0 / 1.0 / 2.0 m from its middle | 0.75 m (the octagon) | 2.7 / 1.86 / 0.75 m; a run at it climbs the rim, the bowl and the column |
| `anim_quality.py`; `effort.py --profile arcade`; the demo | 38 / 45; 8 / 8 | 38 / 45; 8 / 8; the body's path identical |
| `climb_check`, `traversal_check` | | identical (37 / 37, 19 / 19 lanes) |

## Decision

KEEP: `hang_side_dead_zone` (0, off, by default); in the showcase 0.5, the jump buffer the ground's alone, the fall
blended from ready to flailing, the fountain solid.

## Rejected / not done

* Making the hang's thresholds relative to the intent's length throughout (a stick half pushed would then climb as a
  full push does): it changes every hang's behaviour for every game; the dead zone fixes the case and is off by default.
* A motion-captured fall: the owner's Mixamo library has only the skydiver; a hand-keyed pair, blended by time and the
  ground, reads as a fall without it.
* The fountain as a trimesh of its model: two cylinders cover what a body meets, and a run at it climbs it like the
  houses (mantles onto the rim, the bowl, the column's top).
