# FellowEngine — milestone 153 report (the body's weight while climbing; the city's houses built)

Status: **FINAL** (2026-11-19). Branch `fellow/geometry`. Everything here ran on the CPU and lavapipe in the cloud: the
owner needed the laptop, so its GPU is paused (`f518` held in `.tools/wip`).

## 0. Result

The owner on milestone 152's video: "much better, but 1. the houses further off are still poor and ugly; 2. the
climbing animations are unrealistic, the transitions between them strange — it should come closer to AAA."

| the owner's words | what changed |
|---|---|
| "the houses further off are poor and ugly" | 794 houses built as houses (windows in openings, shutters, cornices, eaves, tiled roofs, terraces, chimneys), each its own colour, in place of boxes with windows drawn on them; haze over the distance |
| "the climbing animations are unrealistic" | Leaps that wind up, spring and are caught with their speed; hips that swing on a spring; the hands thrown to the ledge; a lower braced hang; a climb-up clip of its own; a jump at the wall reaching for the hold |
| "the transitions between them are strange" | No pose cut where the traversal's hints change (eased limbs, the hang's first pose the climb-up's), clips played over each move's own time, eased cross-fades |

* **Decision 0290 (engine, opt-in)**:
  * `FellowTraversal3D.hang_leap_windup`: a leap from a hold winds up (drawn in to the wall, the legs coiled), springs
    from rest and is caught with its speed (slowed by gravity going up). The hands are thrown to the ledge ahead of the
    body at full IK weight — before, their weight fell to 15 % mid-leap and the arms dropped to the clip's, hanging or
    held out level. The feet push off the wall and are braced again as it is caught.
  * `FellowTraversalPose3D.body_sway` (with `sway_frequency`, `sway_damping`, `sway_max`): the hips on a damped spring
    behind the body — they swing on when a leap is caught and lag when it starts, the torso leaning back against them;
    never lower than the arms reach.
  * `limb_smoothing`: the hands' and feet's targets eased where the hints change.
  * `hang_clip`: a mantle from a hang gets a clip of its own (the high mantle's began standing on the ground: the hanging
    body stood up in a frame and floated up the wall).
* **The showcase's climbing**:
  * the hang braced at 0.42 m (0.65 m drew the knees up to the chest), its clips remade for it;
  * the leaps' clips: coiled, the legs driving, together and bent in the air, swung in to the wall when caught; each
    move's clip scaled to the move's time (a time-scale node each);
  * the climb up from a hang: pulled up, the feet walking up the wall, the hands turning to press down, a knee onto the
    top, standing up;
  * the jump at a wall: the arms reaching up and forward for the hold, the knees up (before: the arms flung out);
  * the animation tree's cross-fades eased in and out.
* **The city's houses** (`ac_buildings.py`: `far_house()`, `skyline_houses()`; `build_rooftops.gd`:
  `_skyline_houses()`):
  * 30 prototypes (5 widths × 3 heights × 2), each in a near set and a plainer far one, placed 794 times in the blocks
    as MultiMesh instances; 1.1 million triangles, 1.8 MB of resources;
  * each house: a rusticated or plastered ground floor with shops' arches and doors; windows in real openings (reveals,
    glass, sills), shutters open, closed or half; stone frames and hoods on some; string courses; a top floor's loggia
    on some; balconies with iron railings; a moulded cornice; eaves on rafters; hip or gable roofs with ridge tiles and a
    roll of tiles at the eaves, or a terrace behind a parapet with its altana; chimneys;
  * each house its plaster's colour (the instance's colour, sRGB, in the plaster's shaders), its weathering's seed its
    own; the near ones cast shadows;
  * the scene's fog: a light haze with the sky's colour (aerial perspective) — the city and the hills recede.

## 1. Checks (cloud)

* **`climb_check.gd`**: a new lane `leap_style` (decision 0290): the leap caught 0.15 s later (its wind-up), the hips
  swung 15 cm, never in the wall. The other 36 lanes: value for value on GodotPhysics3D and Jolt.
* **Regression suite**: `climb`, `ledge_corners`, `traversal` exact.
* **The class reference**: the new members documented, in `--doctool`'s order; `xmllint` and `make_rst.py` clean.
* **Pictures** (lavapipe): the climb up CasaCrema before and after, from behind and from beside; the city from the
  roofs, over the square and in a street, before and after the instances' colour was read as sRGB (it had come out
  white: a linear colour where the shader's tint is sRGB).
* **Video**: the climb up CasaCrema from beside the climber and a flight over the city (Movie Maker, lavapipe).

## 2. Next

* The GPU, when the owner allows it: `f518`, then a fix for the closed frame's path (decision 0270).
* The climbing: the hands' fingers on the stone (a grip per hold's shape), the feet finding the wall's actual holds,
  a fall when a leap misses.
* The city: its streets' paving and people beyond the square.
