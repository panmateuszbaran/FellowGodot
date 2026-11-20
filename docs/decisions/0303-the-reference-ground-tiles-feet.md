# 0303 — The reference's ground, tiles and feet: setts in running courses, wide rounded tiles, a slipper of a foot

Status: **KEEP** (the showcase's: its materials, its roofs, its mannequin; nothing in the engine) · Branch
`fellow/geometry` · Code:
* `showcase/rooftops/shaders/paving_pbr.gdshader`: `slabs_anti_tile`, `slabs_bias`, `slabs_normal_strength`,
  `band_strength`; no pavement's flags in `flagged`; less damp and moss by the walls;
* `showcase/tools/build_rooftops.gd`: the setts; `coppo_b` and `coppo_c`;
* `showcase/tools/fetch_textures.py`: `setts`, Poly Haven's `square_cobblestone`;
* `showcase/tools/blender/ac_common.py`: `Builder.coppo`'s `flat`; the materials `coppo_b` and `coppo_c`;
* `ac_buildings.py`: `eaves`' tiles; no chimney on `casa_riferimento`;
* `mannequin.py`: the foot, the toes, the ankle and the shin's foot.

Checks: the ground, the tiles and the feet cropped from the video's frames beside FellowEngine's; the 38 animation
gates.

## Problem

The owner: "I don't like the look of the tiles and the ground in the first scene, match them more to the reference",
and "use these shots to improve the feet too". Beside the video's frames:

* **The ground.** The video's square is laid with oblong setts, about 0.45 × 0.2 m, in running courses, worn smooth,
  warm. Ours was large grey flagstones from a wall's photograph, with white joints. Across them ran pale travertine
  bands every 7 m: white stripes the video has none of. Before the reference's house ran a strip of a pavement's flags
  with dark damp and moss.
* **The tiles.** The video's are wide, low, rounded tiles in columns: a narrow dark valley between them, each course's
  end a thick rounded step, each tile a shade of its own. Ours (decision 0300) were narrow tall tubes over lit pans,
  one colour, reading as ribs.
* **The feet.** The video's mannequin has a slim slipper of a foot, its top falling evenly to a rounded point, a small
  dark ankle half sunk in it. Ours were chunky wedges, 10 cm high and 11 cm across, with a bulbous toe cap, and a gap
  between the shin and the ankle's ball.

## Change

**The ground:**

* The square and the reference's street laid with Poly Haven's `square_cobblestone`: oblong setts in running courses
  (CC0, fetched by `fetch_textures.py` as `setts`), 3.6 m to the texture.
* Its courses kept straight: no turned second sample against tiling.
* Its grain softened: the colour sampled a mip and a half blurred, the normal map at 0.45.
* Warm, the video's sandstone.
* No travertine bands (`band_strength` 0).
* Before the reference's house, the setts run to its wall (no pavement's flags in the `flagged` rectangle).
* Everywhere, the damp and the moss by the walls at half.

**The tiles** (`eaves`), on every roof:

* covers 0.28 m wide and 9 cm high (`coppo`'s `flat`: a low arch), tapering, in columns 0.31 m apart;
* courses 0.4 m, each cover's wide end standing proud of the one below on a thick lip;
* a narrow pan low in each valley, its floor deep and in shade;
* each tile one of three firings (`coppo`, a browner and darker `coppo_b`, an oranger `coppo_c`), its photograph's
  weathering partly kept.

The tiles' geometry was halved where nothing shows: the pans have no lip, and the covers have 7 segments. The reference
house's roof has no chimney, as the video's has none there.

**The feet** (`mannequin.py`):

* 9 cm across (11 before), the forefoot 6 cm high (8.5);
* the top rising round the ankle's ball and falling evenly to a rounded point;
* the toes' piece flush with it;
* the ankle's ball 3.2 cm (3.8), half sunk;
* the shin carried down to it, with no gap.

## Checks

* The ground beside the video's frames 1 and 25: the setts, their courses and their warmth as the video's; the white
  stripes gone; the house's front on clean stone.
* The tiles beside frames 84, 88 and 93: rows of wide rounded tiles, of the size of the video's beside the climber's
  hand, with dark valleys and stepped courses.
* The feet beside frames 1, 25 and 45: slim and low, as the video's.
* `reference162_sbs.mp4`: the whole shot beside the video.
* 38 of 38 animation gates, none worse than milestone 158's baseline.

## Decision

KEEP: the game's square has the video's ground and roofs too.

## Rejected / not done

* A texture for the tiles (Poly Haven's clay roofs) on a flat face: close up, the video's tiles stand in relief.
* Larger, more irregular setts: the photograph's are regular; the video's are a little more worn and uneven.
