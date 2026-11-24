# 0330 — Rooms behind the windows: interior mapping as a material, the rooftops' windows given rooms

Status: **KEEP** — `FellowInteriorMaterial3D` (new, module `fellow_world`; nothing changes without it); the showcase's
rooftops' windows use it · Branch `fellow/geometry` · Code:
* `modules/fellow_world/fellow_interior_material_3d.{h,cpp}` (new): the material, its shader made once (as
  `FogMaterial`'s), registered for FellowEngine's shader baking; `doc_classes/FellowInteriorMaterial3D.xml`;
* `showcase/tools/window_uv2.gd` (new: each window's glass given its UV2), `showcase/tools/build_rooftops.gd` (the
  glass's material and UV2), `showcase/tools/rooftops_windows.gd` (new: the saved models' glass given UV2 without
  Blender), `showcase/rooftops/materials/glass.tres` and the models' meshes;
* `showcase/tools/windows_look.gd`, `showcase/tools/interior_look.gd` (new: the windows looked at);
* `tools/fellow_regression/project/scenes/interiors.tscn` (new scene of the suite).

Checks: the rooftops and a test facade rendered on lavapipe (Mesa's CPU Vulkan), before and after; on the RTX 5070 Ti
laptop at 1920x1080 (jobs f542-f544, once the owner allowed the GPU again) the rooftops' four views before and after
with their render times, and their flicker; the regression suite's new scene; the class reference.

## Problem

The owner asked for something more visible that brings the engine nearer AAA. A city's windows are where a game's
streets look like a film set or like a city: the rooftops' windows were flat — dark glass with a curtain drawn on it
(milestones 158-159), the same at every angle; from the square, a few metres from a window as the player climbs past
it, the glass was a picture of a room, not a room. AAA games' cities (Marvel's Spider-Man, Assassin's Creed Unity, The
Matrix Awakens) put a room behind each window without its geometry — interior mapping (van Dongen 2008): the view's ray
is followed into a box behind the glass and what it meets is drawn there, so the room moves with the eye.

## Change

* **`FellowInteriorMaterial3D`** (a `Material`, like `FogMaterial`: its properties set its shader's uniforms; one shader
  for every instance). In the glass's fragment the view's ray, in the facade's frame (across it: the up axis crossed
  with the glass's normal; up; into the building), meets the room's side walls, floor, ceiling or back wall; and two
  planes across the room before them (furniture across the middle, furniture before the back wall); and a hanging lamp;
  and curtains 10 cm behind the glass. Each room is another by a hash of where it is and `seed`:
  * walls: limewash in eight colours, a damask paper or stripes, wooden panelling or a darker dado, a skirting and a
    cornice; on the back wall a painting in a gilt frame, a door (shut, or open on the next room's dark), a bookcase
    (each book's height and colour its own) or a wardrobe; pictures or mirrors on the side walls;
  * floors: cotto tiles, boards, a herringbone parquet or stone, a rug; ceilings: wooden beams across them or plaster;
  * furniture (`furniture`): a table with two chairs and a vase, a sofa, an armchair with a lamp beside it, a desk;
    before the back wall a sideboard with a lamp or a vase on it, or a bed's head;
  * curtains (`curtain_share`): drapes tied back to either side (open widest above the sill, folds of uneven width),
    or a sheer net; none behind a shop's window or a door's glass (its vertex colour's alpha under 0.5);
  * light: the day through the window (`daylight`, `daylight_color`), falling off into the room to a tenth at its back
    wall, darker in the corners, brighter on the floor near the window; a lamp under the ceiling in `lamp_share` of
    the rooms (`lamp_energy`, `lamp_color`), the others' shade hanging dark;
  * glass: what is seen through it by Fresnel's law (Schlick), its grime (dimmer and rougher in patches), the sky's
    reflection occluded by `reflection` (a street's other houses hide most of it), each pane's normal a little out of
    true (`pane_size`); no direct specular (the sun's disc in old panes whitened whole windows: milestone 159).
* **Every pattern filtered over the pixel's footprint** where the ray meets it (the glass's own footprint, `fwidth` of
  its position, grown with the distance the ray travels into the room and with the angle it meets a surface at): edges
  box-filtered, repeating patterns (tiles, boards, stripes, panels, shelves, beams) as box-filtered pulse trains, the
  books, the tiles' and boards' tones, the damask's and the curtains' folds fading to their means — a pattern finer
  than a pixel is its average instead of a different sample each TAA-jittered frame.
* **Where the rooms are**: `rooms_from_uv2` (on) — each window its own room, centred on it, from its glass's UV2: the
  metres right of its middle and up from its room's floor (0.9 m under a window's bottom, less for a tall one so that
  it fits under the ceiling); off — a grid of rooms along the facade in the mesh's space (`room_size`,
  `floor_height`). Glass facing up or down shows no room.
* **The rooftops' windows**: their glass given UV2 per window (`tools/window_uv2.gd`: the glass's connected pieces, each
  in its facade's frame) when the scene is built, and in the saved models (`tools/rooftops_windows.gd`: 68 meshes — the
  square's houses and the city's), a tall window's glass (its room's floor at its bottom: a shop's arch, a door) with a
  vertex colour's alpha of 0; `rooftops/materials/glass.tres` is the material (no lamps by day, as before).

## Measurements

The RTX 5070 Ti laptop, 1920x1080, the rooftops (`windows_look.gd --timing=1`: 120 frames after 60 to settle, twice
each), the frame's GPU time with the rooms against the old glass (decision 0300's dark panes):

| view | rooms (ms) | old glass (ms) |
|---|---:|---:|
| east (across the square) | 8.23, 8.12 | 8.36, 8.53 |
| north | 7.43, 7.63 | 7.34, 7.66 |
| close (a palazzo's windows) | 7.57, 7.78 | 7.83, 8.28 |
| street | 5.33, 5.40 | 5.44, 5.52 |

No cost to be measured (within the runs' spread; the old glass's shader drew its curtains too).

Flicker (`flicker_metrics.py`, decision 0331's measure: the buildings' pixels whose second temporal difference exceeds
3 levels with the camera still, and whose motion-compensated residual does as it moves at 1.4 m/s): the rooms against
plain dark glass, with decision 0331's stability (and milestone 187's motion softening) — east 0.59 % / 5.52 % against
0.53 % / 5.95 %, north 0.71 % / 6.98 % against 0.73 % / 6.93 %; with milestone 187's TAA 6.54 % against 6.66 % still.
The rooms add none: on lavapipe, before their patterns were filtered, their windows had been among the brightest in the
flicker's map.

The regression suite: `base/interiors` (new: six windows of a wall, lamps in half the rooms, and a block in grid mode).

## How it got there

* The first rooms (daylight 0.6, the glass's grime a dusty albedo) were hazed over: in the golden hour's orange sun the
  grime's albedo lit the glass brighter than the room behind it; its albedo is a fifth of what it was.
* Lit as the scene's light would light them the rooms were nearly black beside sunlit plaster (radiance 0.1 against
  1.8): realistic, and the rooms unseen. They glow with `daylight` 1.0 (as games' interiors do).
* Rooms of plain plaster read as empty boxes; the walls got paper, panelling and props, the floors and ceilings
  patterns, the rooms furniture and a lamp.
* The windows of the rooftops' shops (arches 2.6 m wide, 3.4 m high) put their floor 0.9 m under them and their tops
  through the ceiling: a tall window's room floor is at its bottom and the rooms are 3.6 m high.
* A net across a shop's whole arch read as a shower curtain: no curtains behind a tall window's glass; the nets dimmer
  and thinner.
* MSVC takes a string literal of at most 16 380 bytes (C2026): the shader is adjacent raw literals.

## Decision

KEEP.

## Rejected / not done

* Rooms from cubemaps (prerendered rooms, as Unreal's interior cubemaps): no assets in the repository; the rooms are
  procedural.
* The rooms' furniture as boxes (a ray against each): flat silhouettes on two planes, as layered interior mapping does.
* Sunlight through the window onto the floor (the room does not know the sun's direction).
* The rooftops at night (lamps in the windows): the scene is a golden hour.
