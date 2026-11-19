# 0298 — The reference's house finished as the video's: stone in relief, louvred shutters, casements, coppi, carved corbels, a zinc gutter; its light and lens

Status: **KEEP** (the showcase's: its materials, its models, its scene; nothing in the engine) · Branch
`fellow/geometry` · Code: `showcase/rooftops/shaders/facade_pbr.gdshader` (parallax from a height map),
`window_glass.gdshader` (`lead_width`), `showcase/tools/build_rooftops.gd` (the materials, the environment, the
reference camera's depth of field), `showcase/tools/blender/ac_buildings.py` (`louvred_shutter`, `casement`, `corbel`,
`eaves`' coppi, `casa_riferimento`), `ac_common.py` (`coppo`, `zinc`), `showcase/tools/fetch_textures.py` (the height
maps), `showcase/tools/reference_look.gd` (the comparison) · Checks: the reference shot rendered at the video's frames
beside them (`reference_look.gd`); the 38 animation gates.

## Problem

Milestone 157 (decision 0297) replayed the reference video's performance at a replica of its house. Beside the video
the replica, and the square's houses, were plainly poorer. The owner: "why waste time on the animations when our
models, even the buildings, are clearly far from the reference's quality?"

The failings, frame by frame:

* the walls were flat under a photograph of stone, the blocks twice the video's size;
* the shutters and window frames were plain boxes, the glass lit white with a lead grid;
* the tiles lay only along the eaves, over a photograph of a roof;
* the balconette stood on plain blocks, and the gutter was a thick black bar;
* the image was hard and dark beside the video's soft golden light, everything equally sharp.

## Change

**Relief** (`facade_pbr.gdshader`):

* Parallax occlusion mapping from each stone set's height map. The height maps are Poly Haven's displacement maps, now
  fetched with the rest by `fetch_textures.py`.
* The march runs before the material is sampled, so every map reads the displaced UV.
* `height_lo` and `height_hi` say which band of the height map is the face and which is the joint (the sandstone's
  mortar is a narrow low band), and `parallax_depth` how deep in metres.
* `cavity` darkens the joints' depth (ambient occlusion).
* `albedo_soften` blurs the photograph's own baked shading (a lower mip, `soften_lod`): it fought the parallax's.
* The ashlar is now at the video's block size (2.5 m a texture) and 2.5 cm deep; the rustic stone is 3.5 cm deep; the
  roof's photograph is 6 cm deep between the coppi.

**Geometry** (`ac_buildings.py`):

* `louvred_shutter`: stiles, rails and slats tilted 38°, 5 cm apart.
* `casement`: a window's wooden frame, mullion and transoms, inside the reveal.
* `corbel`: a carved scroll profile, extruded.
* Every non-arched window gets the casement; louvred shutters hang on windows open and closed.
* `eaves` lays real coppi over every roof face: half-tubes in 0.26 m columns and 0.38 m courses, jittered, clipped to
  the face, in a weathered terracotta `coppo`. The roofs' collision follows them (8 cm up); the fascia's collision box
  rises to their lip, so the mantle still finds it.
* `casa_riferimento`:
  * the climbed window: a casement and louvred shutters;
  * its balconette: on three scroll corbels, with a roll under its slab;
  * the eave: 0.25 m out;
  * a half-round gutter of weathered `zinc` (metallic, grey) hung under the lip.

**Glass**: `lead_width` (0 turns the leading off), dark rooms and a faint lamp.

**Light and lens** (`build_rooftops.gd`):

* The environment: ambient 1.55, SSAO 1.3, saturation 0.92, AgX contrast 1.05.
* The reference shot's camera: depth of field beyond 7 m (a 9 m transition, amount 0.06), as the video's lens.
* The demo's `reference` shot: the sky redrawn at once for its sun (`PROCESS_MODE_REALTIME` during the shot: its first
  frames had been hazy), the sun 45° to the front.

**The comparison** (`tools/reference_look.gd`): the reference shot rendered at chosen frames of the video, each after
the light has settled, set beside the video's frames in one sheet.

## Checks

* The sheets beside the video at frames 10, 45, 70, 84 and 93 (and 25, 53, 57, 89, 93), over five rounds of changes.
  * The walls' blocks: their size and their joints' relief now read as the video's.
  * The windows: frames, louvres and dark glass.
  * The roofs: coppi in rows over every face.
  * The corbels carved, the gutter a thin grey half-round.
* 38 of 38 animation gates pass, none worse than milestone 158's own baseline. The roofs' collision was raised to the
  coppi; the mantle's lip kept.
* The scene builds without errors.

## Decision

KEEP.

## Rejected / not done

* Tessellated displacement: parallax gives the joints' depth at no geometry; silhouettes stay flat (seen on edges
  only).
* The light, as the owner then said ("why does our lighting look so nasty, instead of soft and delicate as in the
  reference?"), is still harder, darker and more detailed than the video's. Measured on five frames:
  * the median luminance 0.024-0.08 against 0.08-0.18;
  * the darkest twentieth 2-3 times darker;
  * the fine detail's contrast 1.3-1.6 times the video's;
  * the saturation 106-131 against 87-90.

  Next milestone.
