# 0157 — Motion matching on uneven ground; clips mirrored and tagged

Status: **KEEP** (new options, off by default: exact without them) · Branch `fellow/geometry` · Code:
`modules/fellow_animation/fellow_motion_matcher_3d.{h,cpp}` (`ground_*`, `tags_required`, `tags_excluded`),
`fellow_motion_database.{h,cpp}` (the `mirror`, `mirror_pairs` and `tags` build options, the tag filter of the
search) · Checks: `benchmarks/project/bench/ground_check.gd`, `bench/matching_tags_check.gd`; Matching Lab's
`--matching_mirror=1`, `--matching_require`, `--matching_exclude`.

## Problem

Milestone 48's motion matching (decision 0149) moved its characters over flat ground only: the root stayed at the
height the character was put at, and the feet were where the clips had them. On a slope, stairs or uneven ground a
game had to move the root itself (a CharacterBody3D under the character) and then saw the feet float over the lower
ground and sink into the higher. The AAA assessment (`docs/fellow_aaa_readiness.md`) listed ground adaptation, tags
and mirroring as what motion matching lacked: every database had only what was captured (a left turn and no right
one, unless both were captured), and gameplay could not keep the search to a set of clips (crouching, carrying,
injured).

## Change

**Ground adaptation** (`ground_adaptation`, off by default). Before each update, on the main thread (`advance()`, and
`advance_group()` before it hands the updates to the worker threads, which must not query the physics space), rays
straight down against the physics world (`ground_collision_mask`) from `ground_probe_height` (0.6 m) above to as far
below the character's ground: under the root, and under each foot's heel (its ankle) and toe where they were shown
last. Then

* the root follows the ground's height under it (eased, a critically damped spring of twice `ground_halflife`,
  0.05 s);
* each foot is placed on the highest ground under it — its heel's or its toe's — by adding that ground's height over
  the root's to the animated foot, eased, up onto higher ground four times as fast as down (a foot landing on a step
  must not sink into it); the leg's two-bone IK reaches it, with foot locking as before;
* the hips are lowered as far as the lower foot must reach down (the leg over the higher ground then bends instead of
  the lower one overstretching);
* a foot that is down turns to the ground's slope (`ground_alignment`, at most 35°).

`ground_foot_placement = false` leaves only the root following the ground, as a game without foot placement has it.
On flat ground the pose is the one without ground adaptation (no IK where there is nothing to reach).

**Mirroring** (`build()` option `mirror`). Each clip again, reflected across the skeleton's YZ plane with its left and
right bones swapped: from the clip's global poses (forward kinematics), each bone's global position with x negated
and its rotation reflected ((x, y, z, w) → (x, −y, −z, w)) given to its pair, corrected by the rests (the pair's rest
against the bone's rest reflected: rigs whose two sides' bone axes are not mirror images mirror too), and back to
local poses. Pairs by name ("Left"/"Right", ".L"/".R", "_l"/"_r"…) or `mirror_pairs`. The mirrored clips are then
sampled like the others: roots, contacts, features.

**Tags** (`build()` option `tags`: a clip's name to its tags; a mirrored clip has its clip's and `"mirrored"`; at most
31 names). Per frame the clip's tag mask; `FellowMotionMatcher3D.tags_required` (all of them) and `tags_excluded`
(none of them) filter the search (the boxes of the search still bound every frame: a filter only skips frames). When
the clip playing leaves the tags, the next update searches at once.

## Verification

* `bench/ground_check.gd` (headless; Matching Lab's CMU database): one character walking (1.3 m/s, 10 s) or jogging
  (2.8 m/s, 6 s) straight over flat ground, an 8° slope, stairs (eight 15 cm steps up, a landing, eight down) and
  uneven ground (blocks of 0-12 cm, 50 cm square), with ground adaptation off, root only and on. The same inputs on
  every ground, so the animation is the same frame for frame (the search does not see the ground's height): each
  foot's clearance — its heel's or its toe's, whichever is nearer its own on flat ground — against the same frame's on
  flat ground, in the frames the foot is down there (its ankle under 13 cm).
* `bench/matching_tags_check.gd`: every mirrored frame's features against its original's reflected (the feet
  swapped, x negated); one character walking and turning for 30 s with tags required or excluded.
* Matching Lab's evaluation (`bench/matching_eval.gd`, 16 characters, 60 s) with and without mirrored clips.
* The class reference against the engine (`--doctool`: no difference), `make_rst.py` (no warning).

## Results

Ground adaptation, a foot's clearance against flat ground's in its stance frames (walking; mean, 95th percentile and
largest error in millimetres; the share of frames with a part of the foot 3 cm or more in the ground):

| ground | root only (`ground_foot_placement` off) | on |
|---|---|---|
| flat | 0 · 0 · 0 · 0 % | 0 · 0 · 0 · 0 % (the same pose as off) |
| slope 8° | 28.2 · 76.2 · 81.1 · 49 % | **3.1 · 7.7 · 11.4 · 0 %** |
| stairs 15 cm | 56.0 · 158.8 · 200.3 · 31 % | **12.6 · 52.5 · 152.2 · 2.9 %** |
| uneven 0-12 cm | 26.7 · 70.2 · 90.3 · 14.7 % | **10.0 · 30.1 · 92.8 · 0.9 %** |

Jogging: slope 28.9 → 4.6 mm (in the ground 42 % → 0 %), stairs 53.3 → 14.6 mm (23 % → 0.9 %), uneven 35.0 →
12.7 mm (19 % → 3.1 %). With ground adaptation off the root stays at its height (on the slope the feet end 1.1 m in
the ground). The hips are lowered by up to 141 mm on the stairs, 62 mm on the uneven ground, 20 mm on the slope. On
the way to these figures (measured then by the ankle's clearance alone): the feet eased up as fast as down left a
foot in the stairs' steps in 12.9 % of the frames, eased up four times as fast 3.2 %, with a probe under the toe
besides the heel's 1.5 %.

Mirroring and tags (Matching Lab's database: 158 clips, 98 109 frames; mirrored 316 clips, 196 218 frames, built in
6.2 s):

| check | result |
|---|---|
| mirrored features against the originals' reflected (88 629 frames) | no difference at 0.1 mm or 0.1 mm/s |
| `tags_required = ["s035"]` (a CMU subject) | 23 clips played, **0 frames** of others |
| `tags_excluded = ["mirrored"]` | 38 clips, no mirrored frame |
| `tags_required = ["s069", "mirrored"]` | 16 clips, every frame mirrored subject 69 |
| no tags, mirrored clips in the database | 40 clips, 53 % of the frames mirrored |

Matching Lab, 16 characters, 60 s:

| | without mirrored clips | with them |
|---|---|---|
| trajectory error (character to simulation) | 38.5 mm | **36.0 mm** |
| foot sliding in contact | 109 mm/s | **90 mm/s** |
| popping (99th, 99.9th percentile) | 35.8, 68.8 mm | 35.3, 71.8 mm |
| transitions a character-second | 4.90 | 4.81 |
| a search (50th, 99th percentile) | 88, 213 µs | 168, 368 µs |
| a 60 Hz step for 16 characters (main thread) | 0.50 ms | 0.67 ms |

## Decision

**KEEP**, all three off by default. Ground adaptation puts the feet on slopes, stairs and uneven ground (on the slope
from half the stance frames in the ground to none); mirroring halves what must be captured and makes the motion
follow better (−6 % trajectory error, −18 % foot sliding) at twice the search cost; tags give gameplay its states.
Not done: probes ahead of a swinging foot (it is placed where it lands, from the frame it is there: the stairs'
largest errors are the foot landing on a step's edge), the ground's slope in the search (a database of uphill and
downhill walking could be searched by it), tags on frame ranges within a clip (Animation markers), and the neural
motion of `FellowMotionCharacter3D` on uneven ground.
