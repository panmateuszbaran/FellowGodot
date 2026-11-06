# FellowEngine — milestone 56 report (motion matching on uneven ground; mirrored and tagged clips)

Status: **checked on the cloud's CPU** (headless, 2026-11-06). Branch `fellow/geometry`. Decision record
`docs/decisions/0157-motion-matching-ground-mirror-tags.md`.

## 0. Result

Point 5 of the plan towards AAA: what motion matching lacked. A FellowMotionMatcher3D now fits its character to the
ground (`ground_adaptation`): the root follows the ground's height, the feet are placed on slopes, steps and uneven
ground by the legs' IK, the hips are lowered for the lower foot, a foot that is down turns to the slope. Against a
root that merely follows the ground, a walking character's feet in their stance frames are 3 mm from where they belong
on an 8° slope instead of 28 mm (in the ground in none of the frames instead of half), 13 mm on stairs instead of 56,
10 mm on uneven ground instead of 27. A database can mirror its clips (`mirror`: twice the motion from the same
capture; Matching Lab's characters follow their paths 6 % closer with 18 % less foot sliding, at twice the search
time) and tag them (`tags`; the matcher's `tags_required` and `tags_excluded` keep the search to a set of clips).
All three are off by default: without them nothing changes.

## 1. Development

* `fellow_motion_matcher_3d.{h,cpp}`: `ground_adaptation`, `ground_collision_mask`, `ground_probe_height`,
  `ground_halflife`, `ground_foot_placement`, `ground_alignment`; rays on the main thread before an update (also in
  `advance_group()`, before its worker threads); `tags_required`, `tags_excluded`; `get_stats()`: `hips_drop_mm`,
  `hips_drop_min_mm`, `ground_offsets_mm`, `toe_positions`.
* `fellow_motion_database.{h,cpp}`: the `mirror`, `mirror_pairs` and `tags` build options; per-frame tag masks; the
  search's and the trajectory search's tag filter; the tags saved with the database (`tag_names`, `range_tags`).
* Matching Lab: `--matching_mirror=1` (its own saved database), tags by CMU subject, `--matching_require`,
  `--matching_exclude`.
* `bench/ground_check.gd`, `bench/matching_tags_check.gd` (headless checks).
* The class reference of both classes.

## 2. Verification

* `ground_check.gd`: one character walking and one jogging over flat ground, a slope, stairs and uneven ground, ground
  adaptation off, root only and on; each foot's clearance against the same frame's on flat ground (the animation is
  the same on every ground: the search does not see the ground's height).
* `matching_tags_check.gd`: the mirrored frames' features against the originals' reflected; tags required and
  excluded.
* Matching Lab's evaluation, 16 characters for 60 s, with and without mirrored clips.
* `--doctool` against the class reference (no difference); `make_rst.py --dry-run` (no warning).

## 3. Measurements

A foot's clearance against flat ground's, stance frames (mean, 95th percentile, largest error in mm; frames with part
of the foot 3 cm or more in the ground):

| ground | walking, root only | walking, on | jogging, root only | jogging, on |
|---|---|---|---|---|
| flat | 0 · 0 · 0 · 0 % | 0 · 0 · 0 · 0 % | 0 · 0 · 0 · 0 % | 0 · 0 · 0 · 0 % |
| slope 8° | 28.2 · 76.2 · 81.1 · 49 % | **3.1 · 7.7 · 11.4 · 0 %** | 28.9 · 84.1 · 94.7 · 42 % | **4.6 · 18.9 · 21.9 · 0 %** |
| stairs (15 cm steps) | 56.0 · 158.8 · 200.3 · 31 % | **12.6 · 52.5 · 152.2 · 2.9 %** | 53.3 · 179.1 · 199.3 · 23 % | **14.6 · 51.0 · 110.3 · 0.9 %** |
| uneven (0-12 cm blocks) | 26.7 · 70.2 · 90.3 · 15 % | **10.0 · 30.1 · 92.8 · 0.9 %** | 35.0 · 90.0 · 93.0 · 19 % | **12.7 · 41.3 · 90.0 · 3.1 %** |

| check | result |
|---|---|
| mirrored features against the originals' reflected | 88 629 frames, no difference at 0.1 mm |
| tags required: one CMU subject; excluded: `mirrored`; required: a subject and `mirrored` | 0 frames played outside the tags in each (23, 38, 16 clips) |
| Matching Lab with mirrored clips (16 characters, 60 s) | trajectory error 38.5 → 36.0 mm, foot sliding 109 → 90 mm/s, popping p99 35.8 → 35.3 mm, a search 88 → 168 µs (p50), the step 0.50 → 0.67 ms |

## 4. Limits

* A swinging foot is placed on the ground under it from the frame it is there, not ahead of it: the stairs' largest
  errors are a foot landing on a step's edge.
* The search does not see the ground's slope (uphill and downhill clips, where captured, are not preferred on slopes).
* Tags are per clip, not per frame range (Animation markers would give ranges).
* Measured headless on the CPU (raycasts against GodotPhysics' boxes); not yet in a lab with characters on terrain,
  nor for FellowMotionCharacter3D's neural motion.

## 5. Next

A Matching Lab variant on terrain (the slice's valley) to look at; the slope in the search.
